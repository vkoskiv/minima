#include <block.h>
#include <vkern.h>

v_ilist block_devices = V_ILIST_INIT(block_devices);

int blockdev_register(struct block_dev *dev) {
	v_ilist_prepend(&dev->base.linkage, &block_devices);
	return 0;
}

int blockdev_unregister(struct block_dev *dev) {
	v_ilist_remove(&dev->base.linkage);
	return 0;
}
