#include <assert.h>
#include <fs/vfs.h>
#include <fs/mem.h>
#include <errno.h>
#include <vkern.h>
#include <sched.h>
#include <kprintf.h>
#include <kmalloc.h>

const char vfs_node_type_chars[nt_symlink + 1] = {
	[nt_unknown]   = '?',
	[nt_file]      = '-',
	[nt_dir]       = 'd',
	[nt_dev_char]  = 'c',
	[nt_dev_block] = 'b',
	[nt_fifo]      = 'f',
	[nt_socket]    = 's',
	[nt_symlink]   = 'l',
};

static struct vfs *root = NULL;

int vfs_mount(struct vfs *fs, const char *mountpoint) {
	if (!root && strcmp(mountpoint, "/") == 0)
		root = fs;
	// TODO: Probably have a vfs_node as first arg to mount op?
	// I could have a node type of nt_mountpoint that then handles
	// traversal between filesystems. That sounds logical, but I might
	// find out pretty quick why that's not a good idea. This code
	// would then get the root entry with fs->ops->get_root() and
	// pass that in?
	return root->ops->mount(root, NULL);
}

int vfs_unmount(struct vfs *fs) {
	(void)fs;
	return -ENOTSUP;
}

// --- FS ops ---

int vfs_lookup(struct vfs_node *dir, const char *name, struct vfs_node **out) {
	if (!name || !out)
		return -EINVAL;
	if (!root->ops->lookup)
		return -ENOTSUP;
	if (!dir)
		dir = vfs_get_cwd();
	if (dir->type != nt_dir)
		return -ENOTDIR;
	return root->ops->lookup(dir, name, out);
}

int vfs_readdir(struct vfs_node *dir, size_t idx, char **name_out) {
	if (!name_out)
		return -EINVAL;
	if (!root->ops->readdir)
		return -ENOTSUP;
	if (!dir)
		dir = vfs_get_cwd();
	return root->ops->readdir(dir, idx, name_out);
}

int vfs_open(struct vfs_node *node, struct vfs_file **out) {
	if (!out)
		return -EINVAL;
	if (!root->ops->open)
		return -ENOTSUP;
	return root->ops->open(node, out);
}

int vfs_close(struct vfs_file *file) {
	if (!file)
		return -EINVAL;
	if (!root->ops->close)
		return -ENOTSUP;
	return root->ops->close(file);
}

int vfs_create(struct vfs_node *dir, const char *name, mode_t mode) {
	if (!name)
		return -EINVAL;
	if (!root->ops->create)
		return -ENOTSUP;
	if (!dir)
		dir = vfs_get_cwd();
	if (!vfs_lookup(dir, name, NULL))
		return -EEXIST;
	return root->ops->create(dir, name, mode);
}

// FIXME: mkdir subdir/another creates a directory with the
// name 'subdir/another'. Need to resolve the path here before calling underlying
// mkdir op, I think.
// I need dirname(), that returns just path, so
// /some/path/to/file -> /some/path/to
// and basename(), which grabs the last thing, so
// /some/path/to/file -> file
int vfs_mkdir(struct vfs_node *dir, const char *name, mode_t mode) {
	if (!name)
		return -EINVAL;
	if (!root->ops->mkdir)
		return -ENOTSUP;
	if (!dir)
		dir = vfs_get_cwd();
	if (!vfs_lookup(dir, name, NULL))
		return -EEXIST;
	return root->ops->mkdir(dir, name, mode);
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

ssize_t vfs_write(struct vfs_file *file, const void *buf, size_t bytes) {
	if (!file->ops->write)
		return -ENOTSUP;
	return file->ops->write(file, buf, bytes);
}

off_t vfs_seek(struct vfs_file *file, off_t offset, int mode) {
	if (!file->ops->seek)
		return -ENOTSUP;
	return file->ops->seek(file, offset, mode);
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
	kprintf("root: %s(%h)\n", lroot->fs->name, lroot->fs, lroot->id);

	int ret;
	assert(!vfs_create(lroot, "hello_world", 0755));
	assert(!vfs_mkdir(lroot, "subdir", 0755));
	assert(!vfs_mkdir(lroot, "bin", 0755));
	assert(!vfs_mkdir(lroot, "dev", 0755));
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

	if (first == second) {
		kprintf("Success! got %s\n", memfs_temp_get_name(first));
	} else {
		kprintf("Ooof\n");
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

static char getchar(struct dev_char *dev) {
	char c;
	int ret = read(dev, &c, 1);
	assert(ret == 1);
	return c;
}

// TODO: Once userland is shaping up, implement devfs and then
// turn terminal.c into a character special device in /dev/tty.
// Then move this code into the line discipline.
ssize_t getline(char **out, size_t n, struct dev_char *dev) {
	char *buf = *out;
	if (!buf) {
		*out = kmalloc(LINE_MAX);
		if (!*out)
			return -ENOMEM;
		buf = *out;
	}
	size_t bytes = 0;
	char c;
	while (bytes < n && (c = getchar(dev)) != '\n') {
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
	v_tok parts = v_tok(line, ' ');
	const char *cmd = v_tok_consume_cstr(&a, &parts);
	C("exit") return 1;
	C("ls") {
		if (v_tok_empty(parts)) {
			return dump_dir(NULL, 0);
		}
		const char *path;
		while ((path = v_tok_consume_cstr(&a, &parts)))
			dump_dir(path, 0);
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
			int ret = vfs_traverse(NULL, container_path, &container_dir);
			if (ret) {
				if (!parents) {
					kprintf("mkdir: %s\n", strerror(ret));
					return ret;
				}
				kprintf("container_path: %s\n", container_path);
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
			kprintf("huh? '%s'\n", path);
			const char *final = basename(&a, path);
			kprintf("final mkdir: %s\n", final);
			ret = vfs_mkdir(container_dir, final, 0777);
			if (ret) {
				kprintf("vfs_mkdir: %s\n", strerror(ret));
				break;
			}
		}
	}
	C("touch") {
		const char *path;
		while ((path = v_tok_consume_cstr(&a, &parts))) {
			int ret = vfs_create(NULL, path, 0777);
			if (ret) {
				kprintf("vfs_create: %s\n", strerror(ret));
				break;
			}
		}
	}
	C("cat") {
		const char *path;
		while ((path = v_tok_consume_cstr(&a, &parts))) {
			struct vfs_node *node;
			int ret = vfs_traverse(NULL, path, &node);
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

			static const size_t readsize = 1;
			// size_t total_read = 0;
			char *buf = kmalloc(readsize + 1);
			ssize_t read_bytes = 0;
			while ((read_bytes = vfs_read(file, buf, readsize))) {
				// total_read += read_bytes;
				kprintf("%s", buf);
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
	return 0;
}

int vfs_debug_shell(void *ctx) {
	(void)ctx;
	struct dev_char *stdin = dev_char_open("keyboard");
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
	dev_char_close(stdin);
	return 0;
}
