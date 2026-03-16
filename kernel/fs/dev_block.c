#include <fs/dev_block.h>
#include <vkern.h>
#include <assert.h>
#include <errno.h>

v_ilist block_devices = V_ILIST_INIT(block_devices);
//FIXME: lock for device list & individual devices

int dev_block_get_block_count(struct dev_block *dev) {
	if (!dev)
		return -EINVAL;
	if (!dev->block_count)
		return -ENOTSUP;
	return dev->block_count((void *)dev);
}

int dev_block_get_block_size(struct dev_block *dev) {
	if (!dev)
		return -EINVAL;
	if (!dev->block_size)
		return -ENOTSUP;
	return dev->block_size((void *)dev);
}

int dev_block_read(struct dev_block *dev, unsigned int lba, unsigned char *out) {
	if (!dev)
		return -EINVAL;
	if (!dev->block_read)
		return -ENOTSUP;
	return dev->block_read((void *)dev, lba, out);
}

int dev_block_write(struct dev_block *dev, unsigned int lba, const unsigned char *in) {
	if (!dev)
		return -EINVAL;
	if (!dev->block_write)
		return -ENOTSUP;
	return dev->block_write((void *)dev, lba, in);
}

int dev_block_register(struct dev_block *dev) {
	v_ilist_prepend(&dev->base.linkage, &block_devices);
	return 0;
}

int dev_block_unregister(struct dev_block *dev) {
	v_ilist_remove(&dev->base.linkage);
	return 0;
}

struct dev_block *dev_block_open(const char *name) {
	v_ilist *pos;
	v_ilist_for_each(pos, &block_devices) {
		struct device *d = v_ilist_get(pos, struct device, linkage);
		if (strcmp(d->name, name) == 0) {
			++d->refs;
			return (struct dev_block *)d;
		}
	}
	return NULL;
}

void dev_block_close(struct dev_block *dev) {
	assert(dev);
	--dev->base.refs;
}
