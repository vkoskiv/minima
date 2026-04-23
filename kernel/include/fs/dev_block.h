#ifndef _DEV_BLOCK_H_
#define _DEV_BLOCK_H_

#include <vkern.h>
#include <device.h>
#include <sync.h>

struct dev_block {
	struct device base;
	struct semaphore blk_sem;
	int (*block_count)(struct device *dev);
	int (*block_size)(struct device *dev);
	int (*block_read)(struct device *dev, unsigned int lba, unsigned char *out);
	int (*block_write)(struct device *dev, unsigned int lba, const unsigned char *in);
};

#endif
