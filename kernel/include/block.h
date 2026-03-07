#ifndef _BLOCK_H_
#define _BLOCK_H_

#include <vkern.h>
#include <device.h>

struct block_dev {
	struct device base;
	int (*block_count)(struct device *dev);
	int (*block_size)(struct device *dev);
	int (*block_read)(struct device *dev, unsigned int lba, char *out);
	int (*block_write)(struct device *dev, unsigned int lba, const char *in);
};

int blockdev_register(struct block_dev *dev);
int blockdev_unregister(struct block_dev *dev);

extern v_ilist block_devices;

#endif
