#include <assert.h>
#include <fs/vfs.h>
#include <fs/mem.h>
#include <fs/ext2/ext2.h>
#include <errno.h>
#include <vkern.h>
#include <sched.h>
#include <kprintf.h>
#include <kmalloc.h>

const char vfs_node_type_chars[nt_mountpoint + 1] = {
	[nt_unknown]   = '?',
	[nt_file]      = '-',
	[nt_dir]       = 'd',
	[nt_dev_char]  = 'c',
	[nt_dev_block] = 'b',
	[nt_fifo]      = 'f',
	[nt_socket]    = 's',
	[nt_symlink]   = 'l',
	[nt_mountpoint] = 'd',
};

static struct vfs *root = NULL;

int vfs_mount(struct vfs *fs, const char *mountpoint) {
	if (!root && strcmp(mountpoint, "/") == 0) {
		int ret = fs->ops->mount(fs);
		if (ret)
			return ret;
		root = fs;
		// OR just leave parent_node == null and interpret that as meaning this.
		root->parent_node = root->ops->get_root(root);
		return 0;
	}
	struct vfs_node *mp;
	int ret = vfs_traverse(NULL, mountpoint, &mp);
	if (ret)
		return ret;
	if (mp->type != nt_dir)
		return -ENOTDIR;
	fs->parent_node = mp;
	fs->parent = mp->fs;
	ret = fs->ops->mount(fs);
	if (ret)
		return ret;
	mp->type = nt_mountpoint;
	mp->fs = fs;
	return 0;
}

int vfs_unmount(struct vfs *fs) {
	if (fs->parent_node) {
		assert(fs->parent_node->type == nt_mountpoint);
		fs->parent_node->type = nt_dir;
		fs->parent_node->fs = fs->parent;
	}
	int ret = fs->ops->unmount(fs);
	if (ret)
		return ret;
	if (fs == root)
		root = NULL;
	return 0;
}

// --- FS ops ---

int vfs_lookup(struct vfs_node *dir, const char *name, struct vfs_node **out) {
	if (!name)
		return -EINVAL;
	if (!dir)
		dir = vfs_get_cwd();
	if (!dir->fs->ops->lookup)
		return -ENOTSUP;
	if (dir->type != nt_dir && dir->type != nt_mountpoint)
		return -ENOTDIR;
	struct vfs_node *node = NULL;
	int ret = dir->fs->ops->lookup(dir, name, &node);
	if (ret)
		return ret;
	assert(node);
	if (node->type == nt_mountpoint) {
		if (strcmp(name, "..") == 0) {
			// HACK, patching in the parent fs for a lookup, then restoring it.
			// 99% sure this is idiotic, but it works for now.
			struct vfs *stash = node->fs;
			node->fs = node->fs->parent;
			ret = vfs_lookup(node, name, out);
			node->fs = stash;
			return ret;
		} else {
			node = node->fs->ops->get_root(node->fs);
		}
	}
	if (out)
		*out = node;
	return 0;
}

int vfs_readdir(struct vfs_node *dir, size_t idx, char **name_out) {
	if (!name_out)
		return -EINVAL;
	if (!dir)
		dir = vfs_get_cwd();
	if (!dir->fs->ops->readdir)
		return -ENOTSUP;
	return dir->fs->ops->readdir(dir, idx, name_out);
}

int vfs_open(struct vfs_node *node, struct vfs_file **out) {
	if (!out)
		return -EINVAL;
	struct vfs *fs = node->fs;
	if (!fs->ops->open)
		return -ENOTSUP;
	return fs->ops->open(node, out);
}

int vfs_close(struct vfs_file *file) {
	if (!file)
		return -EINVAL;
	struct vfs *fs = file->node->fs;
	if (!fs->ops->close)
		return -ENOTSUP;
	return fs->ops->close(file);
}

int vfs_create(struct vfs_node *dir, const char *name, mode_t mode) {
	if (!name)
		return -EINVAL;
	if (!dir)
		dir = vfs_get_cwd();
	if (!dir->fs->ops->create)
		return -ENOTSUP;
	if (!vfs_lookup(dir, name, NULL))
		return -EEXIST;
	return dir->fs->ops->create(dir, name, mode);
}

// FIXME: mkdir subdir/another creates a directory with the
// name 'subdir/another'. Need to resolve the path here before calling underlying
// mkdir op, I think.
int vfs_mkdir(struct vfs_node *dir, const char *name, mode_t mode) {
	if (!name)
		return -EINVAL;
	if (!dir)
		dir = vfs_get_cwd();
	if (!dir->fs->ops->mkdir)
		return -ENOTSUP;
	if (!vfs_lookup(dir, name, NULL))
		return -EEXIST;
	return dir->fs->ops->mkdir(dir, name, mode);
}

struct vfs_node *vfs_get_root(void) {
	return root->ops->get_root(root);
}

// --- File ops ---

ssize_t vfs_read(struct vfs_file *file, void *buf, size_t bytes) {
	if (!file->ops->read)
		return -ENOTSUP;
	return file->ops->read(file, buf, bytes);
}

ssize_t vfs_read_at(struct vfs_file *file, void *buf, size_t bytes, off_t at) {
	if (!file->ops->read_at)
		return -ENOTSUP;
	return file->ops->read_at(file, buf, bytes, at);
}

ssize_t vfs_write(struct vfs_file *file, const void *buf, size_t bytes) {
	if (!file->ops->write)
		return -ENOTSUP;
	return file->ops->write(file, buf, bytes);
}

ssize_t vfs_write_at(struct vfs_file *file, const void *buf, size_t bytes, off_t at) {
	if (!file->ops->write_at)
		return -ENOTSUP;
	return file->ops->write_at(file, buf, bytes, at);
}

off_t vfs_seek(struct vfs_file *file, off_t offset, int mode) {
	if (!file->ops->seek)
		return -ENOTSUP;
	return file->ops->seek(file, offset, mode);
}

int vfs_stat(struct vfs_file *file, struct vfs_stat *out) {
	if (!file->ops->stat)
		return -ENOTSUP;
	return file->ops->stat(file, out);
}

// --- Higher-level VFS abstractions ---

int vfs_traverse(struct vfs_node *from, const char *path, struct vfs_node **out) {
	if (!path)
		return -EINVAL;
	struct vfs_node *cur = from;
	if (path[0] == '/')
		cur = vfs_get_root();
	else if (!cur) {
		if (!current->cwd)
			current->cwd = vfs_get_root();
		cur = current->cwd;
	}
	assert(cur);
	// kprintf("Traversing path: '%s'\n", path);
	v_tok split = v_tok(path, '/');
	v_tok part = { 0 };
	while ((part = v_tok_consume(&split), !v_tok_empty(part))) {
		// FIXME: hack, toks aren't null-terminated and I can't be bothered to add
		// name len to all those APIs above atm.
		char *name = strndup(part.beg, v_tok_len(part));
		struct vfs_node *next;
		int ret = vfs_lookup(cur, name, &next);
		if (ret) {
			kfree(name);
			return ret;
		}
		cur = next;
		kfree(name);
	}
	if (out)
		*out = cur;
	return 0;
}

// TODO: traverse to / if !path?
int vfs_chdir(const char *path) {
	if (!path)
		return -EINVAL;
	struct vfs_node *new;
	int ret = vfs_traverse(NULL, path, &new);
	if (ret)
		return ret;
	assert(new);
	if (new->type != nt_dir)
		return -ENOTDIR;
	// TODO: refcount nodes?
	// i.e. vfs_put(current->cwd); or something here.
	current->cwd = new;
	return 0;
}

struct vfs_node *vfs_get_cwd(void) {
	if (!current->cwd)
		current->cwd = vfs_get_root();
	return current->cwd;
}

struct vfs_file *vfs_open_file(const char *path) {
	struct vfs_node *node;
	int ret = vfs_traverse(NULL, path, &node);
	if (ret)
		return NULL;
	struct vfs_file *f;
	ret = vfs_open(node, &f);
	if (ret)
		return NULL;
	return f;
}

// --- Debug code ---

#include <fs/dev_char.h>

static void indent(int depth) {
	for (int i = 0; i < depth; ++i)
		kprintf("  ");
}

static void dump_perms(const struct vfs_node *node) {
	kput(vfs_node_type_chars[node->type]);
	mode_t mode = node->mode;
	for (int i = 0; i < 3; ++i) {
		kput(mode & 00400 ? 'r' : '-');
		kput(mode & 00200 ? 'w' : '-');
		kput(mode & 00100 ? 'x' : '-');
		mode <<= 3;
	}
}

size_t count_subdirs(struct vfs_node *dir) {
	if (dir->type != nt_dir)
		return 0;
	int ret;
	size_t dirs = 0;
	size_t idx = 0;
	char *cur_name;
	while (!(ret = vfs_readdir(dir, idx++, &cur_name))) {
		struct vfs_node *node;
		ret = vfs_lookup(dir, cur_name, &node);
		if (ret) {
			kprintf("count_subdirs failed to look up '%s'\n", cur_name);
			kfree(cur_name);
			continue;
		}
		if (!node) {
			kprintf("count_subdirs got NULL node for '%s'\n", cur_name);
			kfree(cur_name);
			continue;
		}
		if (node->type == nt_dir)
			dirs++;
		kfree(cur_name);
	}
	return dirs;
}

static void dump_recursive(struct vfs_node *dir, int depth) {
	int ret;
	// FIXME: rethink idx somewhat. Should maybe handle ./.. on vfs level
	// and always pass starting at 0 to underlying readdir impl.
	size_t idx = 2;
	char *cur_name;
	while (!(ret = vfs_readdir(dir, idx++, &cur_name))) {
		struct vfs_node *node;
		ret = vfs_lookup(dir, cur_name, &node);
		if (ret) {
			kprintf("dump_recursive failed to look up '%s'\n", cur_name);
			kfree(cur_name);
			continue;
		}
		if (!node) {
			kprintf("dump_recursive got NULL node for '%s'\n", cur_name);
			kfree(cur_name);
			continue;
		}
		indent(depth);
		dump_perms(node);
		uint32_t size = 0;
		if (node->type == nt_dir)
			size = sizeof(*node);
		// if (node->type == nt_file)
		// 	size = node->
		kprintf(" %u todo todo %u 2026-04-13 12:00 %s\n", count_subdirs(node), size, cur_name);
		kfree(cur_name);
		if (depth >= 0 && idx > 2 && node->type == nt_dir)
			dump_recursive(node, depth + 1);
	}
}

static int dump_dir(const char *dir, int recursive) {
	struct vfs_node *d;
	int ret = 0;
	if (!dir)
		d = vfs_get_cwd();
	else {
		ret = vfs_traverse(NULL, dir, &d);
		if (ret)
			return ret;
	}
	dump_recursive(d, recursive ? 1 : -1);
	return ret;
}

#include <fs/mem.h>
int vfs_test(void *ctx) {
	(void)ctx;
	/*
		This is just the initial test code I put in stage1 init()
		for initial testing when I was working on the VFS API and memfs.
		I then implemented the pseudo-shell type thing below, but I want
		to check this in too to show early testing.
	*/
	struct vfs_node *lroot = vfs_get_root();
	kprintf("root: %s(%h, %u)\n", lroot->fs->name, lroot->fs, lroot->id);

	int ret;
	assert(!vfs_create(lroot, "hello_world", 0755));
	assert(!vfs_mkdir(lroot, "subdir", 0755));
	assert(!vfs_mkdir(lroot, "bin", 0755));
	assert(!vfs_mkdir(lroot, "etc", 0755));
	assert(!vfs_mkdir(lroot, "proc", 0755));
	assert(!vfs_mkdir(lroot, "sys", 0755));

	struct vfs_node *subdir;
	assert(!vfs_lookup(lroot, "subdir", &subdir));

	assert(!vfs_create(subdir, "important.gif", 0755));

	dump_dir("/", 1);

	struct vfs_node *first;
	ret = vfs_traverse(NULL, "/subdir/important.gif", &first);
	if (ret) {
		kprintf("vfs_traverse(/subdir/important.gif) returned %i (%s)\n", ret, strerror(ret));
		return ret;
	}

	struct vfs_node *second;
	ret = vfs_traverse(NULL, "/subdir/../subdir/important.gif", &second);
	if (ret) {
		kprintf("vfs_traverse(/subdir/../subdir/important.gif) returned %i (%s)\n", ret, strerror(ret));
		return ret;
	}

	assert(!vfs_chdir("/"));
	dump_dir(".", 1);
	assert(!vfs_chdir("subdir"));
	dump_dir(".", 1);
	assert(!vfs_chdir("./.."));
	dump_dir(".", 1);

	assert(!vfs_create(lroot, "test.txt", 0755));

	struct vfs_node *test;
	assert(!vfs_traverse(NULL, "/test.txt", &test));
	struct vfs_file *testfile;
	assert(!vfs_open(test, &testfile));

	const char *teststr = "Hello, world!\n";
	size_t teststrlen = strlen(teststr);
	ssize_t written = vfs_write(testfile, teststr, teststrlen);
	assert(written >= 0);
	if ((size_t)written != teststrlen) {
		kprintf("written: %u, len: %u\n", written, teststrlen);
	}

	assert(vfs_seek(testfile, 0, SEEK_SET) == 0);

	char *buf = kmalloc(teststrlen + 10);
	ssize_t read_bytes = vfs_read(testfile, buf, teststrlen + 10);
	kprintf("read_bytes = %i\n", read_bytes);
	if (read_bytes)
		kprintf("read buf: '%s'\n", buf);
	kfree(buf);

	return 0;
}

#define LINE_MAX 4096

static char getchar(struct vfs_file *f) {
	char c;
	int ret = vfs_read(f, &c, 1);
	assert(ret == 1);
	return c;
}

// TODO: Once userland is shaping up, implement devfs and then
// turn terminal.c into a character special device in /dev/tty.
// Then move this code into the line discipline.
ssize_t getline(char **out, size_t n, struct vfs_file *f) {
	char *buf = *out;
	if (!buf) {
		*out = kmalloc(LINE_MAX);
		if (!*out)
			return -ENOMEM;
		buf = *out;
	}
	size_t bytes = 0;
	char c;
	while (bytes < n && (c = getchar(f)) != '\n') {
		if (c == 0x08) { // backspace
			kput(c);
			buf[--bytes] = 0;
		} else {
			kput((buf[bytes++] = c));
		}
	}
	buf[bytes] = 0;
	return bytes;
}

#define C(name) if (strcmp(cmd, (name)) == 0)

static int run_cmd(v_ma a, const char *line, size_t len) {
	if (!len)
		return 0;
	int ret;
	v_tok parts = v_tok(line, ' ');
	const char *cmd = v_tok_consume_cstr(&a, &parts);
	C("exit") return 1;
	C("ls") {
		if (v_tok_empty(parts)) {
			return dump_dir(NULL, 0);
		}
		const char *path;
		while ((path = v_tok_consume_cstr(&a, &parts))) {
			ret = dump_dir(path, 0);
			if (ret) {
				kprintf("ls: %s\n", strerror(ret));
				return ret;
			}
		}
		return 0;
	}
	C("cd") {
		if (v_tok_empty(parts))
			return vfs_chdir("/");
		return vfs_chdir(v_tok_consume_cstr(&a, &parts));
	}
	C("mkdir") {
		int parents = v_tok_eq(v_tok_peek(parts), "-p");
		if (parents)
			v_tok_consume(&parts);
		const char *path;
		while ((path = v_tok_consume_cstr(&a, &parts))) {
			const char *container_path = dirname(&a, path);
			struct vfs_node *container_dir;
			ret = vfs_traverse(NULL, container_path, &container_dir);
			if (ret) {
				if (!parents) {
					kprintf("mkdir: %s\n", strerror(ret));
					return ret;
				}
				v_tok contparts = v_tok(container_path, '/');
				container_dir = current->cwd;
				const char *dir;
				while ((dir = v_tok_consume_cstr(&a, &contparts))) {
					if ((ret = vfs_mkdir(container_dir, dir, 0777)))
						return ret;
					if ((ret = vfs_traverse(container_dir, dir, &container_dir)))
						return ret;
				}
			}
			const char *final = basename(&a, path);
			ret = vfs_mkdir(container_dir, final, 0777);
			if (ret) {
				kprintf("vfs_mkdir: %s\n", strerror(ret));
				break;
			}
		}
	}
	C("touch") {
		// FIXME: Same path traversal as for mkdir. Maybe abstract that then.
		const char *path;
		while ((path = v_tok_consume_cstr(&a, &parts))) {
			struct vfs_node *cont_dir;
			ret = vfs_traverse(NULL, dirname(&a, path), &cont_dir);
			if (ret) {
				kprintf("touch: vfs_traverse: %s\n", strerror(ret));
				return 0;
			}
			ret = vfs_create(cont_dir, path, 0777);
			if (ret) {
				kprintf("vfs_create: %s\n", strerror(ret));
				break;
			}
		}
	}
	C("stat") {
		const char *path;
		while ((path = v_tok_consume_cstr(&a, &parts))) {
			struct vfs_node *node;
			ret = vfs_traverse(NULL, path, &node);
			if (ret) {
				kprintf("stat: vfs_traverse: %s\n", strerror(ret));
				break;
			}
			struct vfs_file *file;
			ret = vfs_open(node, &file);
			if (ret) {
				kprintf("stat: vfs_open: %s\n", strerror(ret));
				break;
			}

			struct vfs_stat sb;
			ret = vfs_stat(file, &sb);
			if (ret) {
				kprintf("stat: vfs_stat: %s\n", strerror(ret));
				break;
			}
			kprintf("\tsize: %u, block_size: %u, offset: %u, id: %u\n"
					"\tmode: %u, links: %u, uid: %u, gid: %u\n"
					"\tatime: %u\n"
					"\tmtime: %u\n"
					"\tctime: %u\n",
					sb.size, sb.block_size, sb.offset, sb.id,
					sb.mode, sb.links, sb.uid, sb.gid,
					sb.atime, sb.mtime, sb.ctime);
			vfs_close(file);
		}
	}
	C("cat") {
		const char *path;
		while ((path = v_tok_consume_cstr(&a, &parts))) {
			struct vfs_node *node;
			ret = vfs_traverse(NULL, path, &node);
			if (ret) {
				kprintf("cat: vfs_traverse: %s\n", strerror(ret));
				break;
			}
			struct vfs_file *file;
			ret = vfs_open(node, &file);
			if (ret) {
				kprintf("cat: vfs_open: %s\n", strerror(ret));
				break;
			}

			static const size_t readsize = 128;
			// size_t total_read = 0;
			char *buf = kmalloc(readsize + 1);
			ssize_t read_bytes = 0;
			while ((read_bytes = vfs_read(file, buf, readsize))) {
				if (read_bytes < 0) {
					kprintf("vfs_read: %s\n", strerror(read_bytes));
					break;
				}
				// total_read += read_bytes;
				for (ssize_t i = 0; i < read_bytes; ++i)
					kput(buf[i]);
			}
			// kprintf("\ntotal: %u bytes\n", total_read);
			kfree(buf);
			vfs_close(file);
		}
	}
	C("dirname")
		kprintf("%s\n", dirname(&a, v_tok_consume_cstr(&a, &parts)));
	C("basename")
		kprintf("%s\n", basename(&a, v_tok_consume_cstr(&a, &parts)));
	C("mount") {
		const char *path = v_tok_consume_cstr(&a, &parts);
		if (!path) {
			kprintf("Usage: mount <path>\n");
			return 1;
		}
		struct vfs *new = memfs_new();
		if (!new) {
			kprintf("failed to create new memfs\n");
			return 1;
		}
		ret = vfs_mount(new, path);
		if (ret) {
			kprintf("vfs_mount() returned %i\n", ret);
			// FIXME: return value of unmount?
			// Would it make more sense to have a separate memfs_destroy() that frees the
			// core datastructure, instead of doing it in unmount()?
			// FIXME: use vfs_unmount() here?
			new->ops->unmount(new);
			return ret;
		}
	}
	C("e2mount") {
		const char *dev = v_tok_consume_cstr(&a, &parts);
		const char *path = v_tok_consume_cstr(&a, &parts);
		if (!dev || !path) {
			kprintf("Usage: e2mount <dev> <path>\n");
			kprintf("where <dev> is a blockdevice under /dev, e.g. /dev/fd1\n");
			return 1;
		}
		struct vfs_file *drive = vfs_open_file(dev);
		if (!drive) {
			kprintf("e2mount: failed to open drive '%s'\n", dev);
			return 1;
		}
		struct vfs *e2fs = ext2_new(drive);
		if (!e2fs) {
			kprintf("ext2_new() failed\n");
			return 1;
		}
		ret = vfs_mount(e2fs, path);
		if (ret) {
			kprintf("vfs_mount() returned %i\n", ret);
			// FIXME: return value of unmount?
			// Would it make more sense to have a separate memfs_destroy() that frees the
			// core datastructure, instead of doing it in unmount()?
			// FIXME: use vfs_unmount() here?
			e2fs->ops->unmount(e2fs);
			return ret;
		}
	}
	C("unmount") {
		const char *path = v_tok_consume_cstr(&a, &parts);
		if (!path) {
			kprintf("Usage: unmount <path>\n");
			return 1;
		}
		struct vfs_node *node;
		ret = vfs_traverse(NULL, path, &node);
		if (ret) {
			kprintf("unmount: vfs_traverse: %s\n", strerror(ret));
			return ret;
		}
		struct vfs_node *parent;
		ret = vfs_traverse(node, "..", &parent);
		if (ret) {
			kprintf("unmount: vfs_traverse parent: %s\n", strerror(ret));
			return ret;
		}
		if (node->fs == parent->fs) {
			kprintf("unmount: Not a mount point\n");
			return 0;
		}
		ret = vfs_unmount(node->fs);
		if (ret) {
			kprintf("unmount: vfs_unmount: %s\n", strerror(ret));
			return ret;
		}
	}
	return 0;
}

int vfs_debug_shell(void *ctx) {
	(void)ctx;
	struct vfs_file *stdin = vfs_open_file("/dev/kbd");
	if (!stdin) {
		kprintf("Couldn't open /dev/kbd\n");
		return 1;
	}
	uint8_t *abuf = kmalloc(4096);
	if (!abuf)
		return -ENOMEM;
	v_ma arena = v_ma_from_buf(abuf, 4096);
	char *line = NULL;
	for (;;) {
		kprintf("shell> ");
		ssize_t len = getline(&line, LINE_MAX, stdin);
		kput('\n');
		int ret = run_cmd(arena, line, len);
		if (ret == 1)
			break;
	}
	return vfs_close(stdin);
}
