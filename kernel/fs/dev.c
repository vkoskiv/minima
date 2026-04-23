#include <fs/dev.h>
#include <kmalloc.h>
#include <errno.h>
#include <assert.h>
#include <sync.h>

enum devfs_node_type {
	dev_unknown = 0,
	dev_char,
	dev_block,
};

struct devfs_node {
	struct vfs_node base;
	struct device *dev;
	enum devfs_node_type type;
	struct devfs_node *next;
};

struct devfs {
	struct vfs base;
	struct vfs_node root;
	struct devfs_node *devices;
	struct semaphore devices_lock;
};

static struct devfs *s_devfs = NULL;

static ssize_t devfs_read_char(struct vfs_file *file, void *buf, size_t bytes) {
	struct devfs_node *n = (struct devfs_node *)file->node;
	struct dev_char *dev = (struct dev_char *)n->dev;
	if (!dev->read)
		return 0;
	return dev->read(&dev->base, buf, bytes);
}

static ssize_t devfs_read_block(struct vfs_file *file, void *buf, size_t bytes) {
	return -ENOTSUP;
}

static ssize_t devfs_read(struct vfs_file *file, void *buf, size_t bytes) {
	struct devfs_node *n = (struct devfs_node *)file->node;
	switch (n->type) {
	case dev_char: return devfs_read_char(file, buf, bytes);
	case dev_block: return devfs_read_block(file, buf, bytes);
	default:
		assert(NORETURN);
		break;
	}
	return 0;
}

static ssize_t devfs_write_char(struct vfs_file *file, const void *buf, size_t bytes) {
	struct devfs_node *n = (struct devfs_node *)file->node;
	struct dev_char *dev = (struct dev_char *)n->dev;
	if (!dev->write)
		return 0;
	return dev->write(&dev->base, buf, bytes);
}

static int devfs_get_block_size(struct dev_block *dev) {
	if (!dev->block_size)
		return -ENOTSUP;
	int ret;
	sem_pend(&dev->blk_sem);
	ret = dev->block_size((void *)dev);
	sem_post(&dev->blk_sem);
	return ret;
}

static int devfs_get_block_count(struct dev_block *dev) {
	if (!dev->block_count)
		return -ENOTSUP;
	int ret;
	sem_pend(&dev->blk_sem);
	ret = dev->block_count((void *)dev);
	sem_post(&dev->blk_sem);
	return ret;
}

static ssize_t devfs_write_block(struct vfs_file *file, const void *buf, size_t bytes) {
	return -ENOTSUP;
}

static ssize_t devfs_write(struct vfs_file *file, const void *buf, size_t bytes) {
	struct devfs_node *n = (struct devfs_node *)file->node;
	switch (n->type) {
	case dev_char: return devfs_write_char(file, buf, bytes);
	case dev_block: return devfs_write_block(file, buf, bytes);
	default:
		assert(NORETURN);
		break;
	}
	return 0;
}

static int devfs_stat_char(struct devfs_node *n, struct vfs_stat *out) {
	*out = (struct vfs_stat){
		.size = 0,
		.block_size = 1,
		.offset = 0,
		.id = n->base.id,
		.mode = n->base.mode,
		.links = 1, // FIXME
		.uid = 1000,
		.gid = 1000,
	};
	return 0;
}

static int devfs_stat_block(struct vfs_file *file, struct vfs_stat *out) {
	struct devfs_node *n = (void *)file->node;
	struct dev_block *dev = (struct dev_block *)n->dev;
	int ret = devfs_get_block_size(dev);
	if (ret < 0)
		return ret;
	size_t block_size = ret;
	ret = devfs_get_block_count(dev);
	if (ret < 0)
		return ret;
	size_t block_count = ret;

	*out = (struct vfs_stat){
		.size = block_count * block_size,
		.block_size = block_size,
		.offset = file->offset,
		.id = n->base.id,
		.mode = n->base.mode,
		.links = 1, // FIXME
		.uid = 1000,
		.gid = 1000,
	};
	return 0;
	
}

static int devfs_stat(struct vfs_file *file, struct vfs_stat *out) {
	struct devfs_node *n = (void *)file->node;
	switch (n->type) {
	case dev_char: return devfs_stat_char(n, out);
	case dev_block: return devfs_stat_block(file, out);
	default: return -ENOTSUP;
	}
	return 0;
}

static const struct vfs_file_ops devfs_char_ops = {
	.read = devfs_read,
	.write = devfs_write,
	.stat = devfs_stat,
};

static ssize_t devfs_read_at(struct vfs_file *file, void *buf, size_t bytes, off_t at) {
	struct devfs_node *n = (struct devfs_node *)file->node;
	struct dev_block *dev = (struct dev_block *)n->dev;
	if (!dev->block_read)
		return -ENOTSUP;
	int ret = devfs_get_block_size(dev);
	if (ret < 0)
		return ret;
	size_t block_size = ret;
	assert(bytes == block_size);
	assert(at % block_size == 0);
	return dev->block_read(&dev->base, at / block_size, buf);
}

static ssize_t devfs_write_at(struct vfs_file *file, const void *buf, size_t bytes, off_t at) {
	struct devfs_node *n = (struct devfs_node *)file->node;
	struct dev_block *dev = (struct dev_block *)n->dev;
	if (!dev->block_write)
		return -ENOTSUP;
	int ret = devfs_get_block_size(dev);
	if (ret < 0)
		return ret;
	size_t block_size = ret;
	assert(bytes == block_size);
	assert(at % block_size == 0);
	return dev->block_write(&dev->base, at / block_size, buf);
}

static const struct vfs_file_ops devfs_block_ops = {
	// TODO: .read (with buffering)
	// TODO: .write (with buffering)
	.read_at = devfs_read_at,
	.write_at = devfs_write_at,
	.stat = devfs_stat,
};

int devfs_mount(struct vfs *vfs) {
	struct devfs *fs = (struct devfs *)vfs;
	fs->root = (struct vfs_node){
		.fs = &fs->base,
		.id = 0,
		.mode = 0666,
		.type = nt_dir,
	};
	return 0;
}

int devfs_unmount(struct vfs *vfs) {
	(void)vfs;
	return -ENOTSUP;
}

int devfs_lookup(struct vfs_node *dir, const char *name, struct vfs_node **out) {
	struct devfs *fs = (struct devfs *)dir->fs;
	if (strlen(name) == 1 && *name == '.') {
		*out = dir;
		return 0;
	}
	if (dir == &fs->root && strlen(name) == 2 && strncmp(name, "..", 2) == 0) {
		*out = fs->base.parent_node;
		return 0;
	}
	struct devfs_node *d = fs->devices;
	if (!d)
		return -ENOENT;
	do {
		if (strcmp(d->dev->name, name))
			continue;
		*out = &d->base;
		return 0;
	} while ((d = d->next));
	return -ENOENT;
}

int devfs_readdir(struct vfs_node *dir, size_t idx, char **name_out) {
	struct devfs *fs = (struct devfs *)dir->fs;
	struct devfs_node *d = fs->devices;
	if (!d)
		return -ENOENT;
	idx -= 2;
	size_t cur = 0;
	do {
		if (cur++ != idx)
			continue;
		*name_out = strdup(d->dev->name);
		return 0;
	} while ((d = d->next));
	return -ENOENT;
}

static int devfs_open(struct vfs_node *node, struct vfs_file **out) {
	struct devfs_node *n = (struct devfs_node *)node;
	struct vfs_file *f = kmalloc(sizeof(*f));
	if (!f)
		return -ENOMEM;
	f->node = node;
	f->offset = 0;
	f->next = NULL;
	switch (n->type) {
	case dev_char:
		f->ops = &devfs_char_ops;
		break;
	case dev_block:
		f->ops = &devfs_block_ops;
		break;
	default:
		assert(NORETURN);
		break;
	}
	++n->dev->refs;
	*out = f;
	return 0;
}

static int devfs_close(struct vfs_file *file) {
	struct devfs_node *n = (struct devfs_node *)file->node;
	--n->dev->refs;
	// TODO: do something when refs reaches 0?
	kfree(file);
	return 0;
}

struct vfs_node *devfs_get_root(struct vfs *vfs) {
	struct devfs *fs = (struct devfs *)vfs;
	return &fs->root;
}

static const struct vfs_ops devfs_ops = {
	.mount = devfs_mount,
	.unmount = devfs_unmount,
	.get_root = devfs_get_root,
	.lookup = devfs_lookup,
	.create = NULL,
	.mkdir = NULL,
	.open = devfs_open,
	.close = devfs_close,
	.readdir = devfs_readdir,
};

struct vfs *devfs_get(void) {
	if (s_devfs)
		return &s_devfs->base;
	s_devfs = kzalloc(sizeof(*s_devfs));
	s_devfs->base.name = "devfs";
	s_devfs->base.parent = NULL;
	s_devfs->base.parent_node = NULL;
	s_devfs->base.ops = &devfs_ops;
	s_devfs->devices = NULL;
	sem_init(&s_devfs->devices_lock, 1, "devfs->devices");
	return &s_devfs->base;
}

struct vfs_node *devfs_register_char(const struct dev_char *dev) {
	struct devfs_node *n = kmalloc(sizeof(*n));
	n->base = (struct vfs_node){
		.fs = &s_devfs->base,
		.id = 999,
		.mode = 0666,
		.type = nt_dev_char,
	};
	// FIXME: use n->base->type instead of duplicating here?
	n->type = dev_char;
	n->dev = &dev->base;
	sem_pend(&s_devfs->devices_lock);
	{
		n->next = s_devfs->devices;
		s_devfs->devices = n;
	}
	sem_post(&s_devfs->devices_lock);
	return &n->base;
}
struct vfs_node *devfs_register_block(const struct dev_block *dev) {
	struct devfs_node *n = kmalloc(sizeof(*n));
	n->base = (struct vfs_node){
		.fs = &s_devfs->base,
		.id = 999,
		.mode = 0666,
		.type = nt_dev_block,
	};
	// FIXME: use n->base->type instead of duplicating here?
	n->type = dev_block;
	n->dev = &dev->base;
	sem_pend(&s_devfs->devices_lock);
	{
		n->next = s_devfs->devices;
		s_devfs->devices = n;
	}
	sem_post(&s_devfs->devices_lock);
	return &n->base;
}

int devfs_unregister(struct device *dev) {
	(void)dev;
	// TODO
	return -ENOTSUP;
}
