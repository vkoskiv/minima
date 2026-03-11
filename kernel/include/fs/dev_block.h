#ifndef _DEV_BLOCK_H_
#define _DEV_BLOCK_H_

#include <vkern.h>
#include <device.h>

struct dev_block {
	struct device base;
	int (*block_count)(struct device *dev);
	int (*block_size)(struct device *dev);
	int (*block_read)(struct device *dev, unsigned int lba, char *out);
	int (*block_write)(struct device *dev, unsigned int lba, const char *in);
};

int dev_block_register(struct dev_block *dev);
int dev_block_unregister(struct dev_block *dev);

struct dev_block *dev_block_open(const char *name);
void dev_block_close(struct dev_block *dev);

extern v_ilist block_devices;

#endif
