#include <fs/vfs.h>
#include <kmalloc.h>
#include <vkern.h>
#include <errno.h>
#include <assert.h>

struct memfs_node {
	struct vfs_node base;
	char *name;
	struct memfs_node *up;
	struct memfs_node *next;
	struct memfs_node *children;
	uint8_t *data;
	size_t capacity;
};

struct memfs {
	struct vfs base;
	struct memfs_node *root;
	uint32_t last_id;
};

static struct memfs_node *memfs_node_create(struct memfs *fs,
                                            const char *name,
                                            enum vfs_node_type type) {
	struct memfs_node *n = kzalloc(sizeof(*n));
	n->name = strdup(name);
	n->base.fs = &fs->base;
	n->base.type = type;
	n->base.id = fs->last_id++;
	return n;
}

static int memfs_mount(struct vfs *vfs) {
	struct memfs *fs = (struct memfs *)vfs;
	fs->root = memfs_node_create(fs, "/", nt_dir);
	fs->root->up = fs->base.parent_node ? (struct memfs_node *)fs->base.parent_node : fs->root;
	fs->root->base.mode = 0755;
	return 0;
}

static int memfs_recurse_free(struct vfs_node *dir) {
	int ret;
	size_t idx = 2;
	char *cur_name;
	while (!(ret = vfs_readdir(dir, idx++, &cur_name))) {
		struct vfs_node *node;
		assert(!vfs_lookup(dir, cur_name, &node));
		if (node->type == nt_dir)
			memfs_recurse_free(node);
		struct memfs_node *mfn = (struct memfs_node *)node;
		if (mfn->data)
			kfree(mfn->data);
		kfree(mfn);
	}
	return 0;
}

static int memfs_unmount(struct vfs *vfs) {
	struct memfs *fs = (struct memfs *)vfs;
	struct vfs_node *dir = &fs->root->base;
	int ret = memfs_recurse_free(dir);
	if (ret)
		return ret;
	kfree(fs);
	return 0;
}

static struct vfs_node *memfs_get_root(struct vfs *vfs) {
	struct memfs *fs = (struct memfs *)vfs;
	return &fs->root->base;
}

static int memfs_lookup(struct vfs_node *dir, const char *name, struct vfs_node **out) {
	struct memfs_node *d = (struct memfs_node *)dir;
	if (strlen(name) == 1 && *name == '.') {
		*out = &d->base;
		return 0;
	}
	if (strlen(name) == 2 && strncmp(name, "..", 2) == 0) {
		*out = &d->up->base;
		return 0;
	}
	for (struct memfs_node *c = d->children; c; c = c->next) {
		if (strcmp(c->name, name) == 0) {
			if (out)
				*out = &c->base;
			return 0;
		}
	}
	return -ENOENT;
}

static int memfs_readdir(struct vfs_node *dir, size_t idx, char **name_out) {
	struct memfs_node *d = (struct memfs_node *)dir;
	if (idx == 0) {
		*name_out = strdup(".");
		return 0;
	}
	if (idx == 1) {
		*name_out = strdup("..");
		return 0;
	}
	idx -= 2;
	size_t i = 0;
	for (struct memfs_node *c = d->children; c; c = c->next) {
		if (i == idx) {
			*name_out = strdup(c->name);
			return 0;
		}
		i++;
	}
	return -ENOENT;
}

static void memfs_add_child(struct memfs_node *parent, struct memfs_node *child) {
	child->up = parent;
	child->next = parent->children;
	parent->children = child;
}

static int memfs_create(struct vfs_node *dir, const char *name, mode_t mode) {
	(void)mode;
	struct memfs *fs = (struct memfs *)dir->fs;
	struct memfs_node *parent = (struct memfs_node *)dir;
	struct memfs_node *new = memfs_node_create(fs, name, nt_file);
	new->base.mode = mode;
	memfs_add_child(parent, new);
	return 0;
}

static int memfs_mkdir(struct vfs_node *dir, const char *name, mode_t mode) {
	(void)mode;
	struct memfs *fs = (struct memfs *)dir->fs;
	struct memfs_node *parent = (struct memfs_node *)dir;
	struct memfs_node *new = memfs_node_create(fs, name, nt_dir);
	new->base.mode = mode;
	memfs_add_child(parent, new);
	return 0;
}

static ssize_t memfs_read(struct vfs_file *file, void *buf, size_t bytes) {
	struct memfs_node *n = (struct memfs_node *)file->node;
	if (file->offset >= n->capacity)
		return 0;
	size_t remain = n->capacity - file->offset;
	size_t to_read = bytes <= remain ? bytes : remain;
	memcpy(buf, n->data + file->offset, to_read);
	file->offset += to_read;
	return to_read;
}

// TODO from lseek.2 manpage:
// If the O_APPEND file status flag is set on the open file description, then a write(2) always moves the file offset to the end of the file, regardless of the use of lseek().
static ssize_t memfs_write(struct vfs_file *file, const void *buf, size_t bytes) {
	struct memfs_node *n = (struct memfs_node *)file->node;
	size_t end = file->offset + bytes;
	if (end > n->capacity) {
		size_t newsize = end;
		uint8_t *newbuf = kmalloc(newsize);
		if (!newbuf)
			return -ENOMEM;
		// FIXME: krealloc() is unimplemented. This gets pretty
		// heavy since it temporarily needs to retain the old buffer
		// while copying to the new one.
		memcpy(newbuf, n->data, n->capacity);
		kfree(n->data);
		n->data = newbuf;
		n->capacity = newsize;
	}
	// LEFTOFF was testing write for the first time. seems like n->data is
	// garbage here, I don't think I ever zero-init it or alloc an initial buf.
	// Made good progress on this today. Mega-tired now, though.
	memcpy(n->data + file->offset, buf, bytes);
	// Not needed?
	// if (end > n->vfs_node.size)
	// 	n->vfs_node.size = end;
	file->offset += bytes;
	return bytes;
}

static off_t memfs_seek(struct vfs_file *file, off_t offset, int mode) {
	// FIXME: Pretty sure this offset stuff could be moved to vfs_seek(), no?
	struct memfs_node *n = (void *)file->node;
	switch (mode) {
	case SEEK_SET:
		file->offset = offset;
		break;
	case SEEK_CUR:
		file->offset += offset;
		break;
	case SEEK_END:
		file->offset = n->capacity + offset; // TODO: Pretty sure this is incorrect
		break;
	}
	return file->offset;
}

static int memfs_stat(struct vfs_file *file, struct vfs_stat *out) {
	struct memfs_node *n = (void *)file->node;
	*out = (struct vfs_stat){
		.size = n->capacity,
		.block_size = 512,
		.offset = file->offset,
		.id = n->base.id,
		.mode = n->base.mode,
		.links = 0,
		.uid = 1000, // FIXME
		.gid = 1000,
	};
	return 0;
}

static const struct vfs_file_ops memfs_file_ops = {
	.read = memfs_read,
	.write = memfs_write,
	.seek = memfs_seek,
	.stat = memfs_stat,
};

// TODO: flags, like O_APPEND that sets f->offset to end of data
static int memfs_open(struct vfs_node *node, struct vfs_file **out) {
	struct vfs_file *f = kmalloc(sizeof(*f));
	if (!f)
		return -ENOMEM;

	f->node = node;
	f->offset = 0;
	f->next = NULL;
	f->ops = &memfs_file_ops;

	*out = f;
	return 0;
}

static int memfs_close(struct vfs_file *file) {
	kfree(file);
	return 0;
}

static const struct vfs_ops memfs_ops = {
	.mount = memfs_mount,
	.unmount = memfs_unmount,
	.get_root = memfs_get_root,
	.lookup = memfs_lookup,
	.create = memfs_create,
	.mkdir = memfs_mkdir,
	// TODO: rmdir
	// TODO: rm, or I guess unlink?
	.open = memfs_open,
	.close = memfs_close,
	.readdir = memfs_readdir,
};

struct vfs *memfs_new(void) {
	struct memfs *fs = kzalloc(sizeof(*fs));
	fs->base.name = "memfs";
	fs->base.parent = NULL;
	fs->base.parent_node = NULL;
	fs->base.ops = &memfs_ops;
	return &fs->base;
}
