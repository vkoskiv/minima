
/*
	Attempt to write something that resembles a block device
*/

#include <stdint.h>

struct blockdev_sim;

int blockdev_create(const char *image_path, struct blockdev_sim **dev);
int blockdev_destroy(struct blockdev_sim *bdev);
uint32_t blockdev_block_count(struct blockdev_sim *bdev);
uint32_t blockdev_block_size(struct blockdev_sim *bdev);
int blockdev_block_read(struct blockdev_sim *bdev, uint32_t lba, char *out);
int blockdev_block_write(struct blockdev_sim *bdev, uint32_t lba, const char *in);
