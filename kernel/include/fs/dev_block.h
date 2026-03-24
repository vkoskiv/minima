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

int dev_block_get_block_count(struct dev_block *dev);
int dev_block_get_block_size(struct dev_block *dev);
int dev_block_read(struct dev_block *dev, unsigned int lba, unsigned char *out);
int dev_block_write(struct dev_block *dev, unsigned int lba, const unsigned char *in);

int dev_block_register(struct dev_block *dev);
int dev_block_unregister(struct dev_block *dev);

struct dev_block *dev_block_open(const char *name);
void dev_block_close(struct dev_block *dev);

#endif
