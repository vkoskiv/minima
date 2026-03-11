#include <fs/dev_block.h>
#include <vkern.h>
#include <assert.h>

v_ilist block_devices = V_ILIST_INIT(block_devices);

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
