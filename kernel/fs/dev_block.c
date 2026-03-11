#include <fs/dev_block.h>
#include <vkern.h>

v_ilist block_devices = V_ILIST_INIT(block_devices);

int dev_block_register(struct dev_block *dev) {
	v_ilist_prepend(&dev->base.linkage, &block_devices);
	return 0;
}

int dev_block_unregister(struct dev_block *dev) {
	v_ilist_remove(&dev->base.linkage);
	return 0;
}
