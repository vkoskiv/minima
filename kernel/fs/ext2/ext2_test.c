#include <stdio.h>
#include "blockdev.h"
#include "ext2.h"

int main(int argc, char **argv) {
	fprintf(stderr, "ext2 test\n");
	// struct blockdev_sim *bd;
	// int ret = blockdev_create("clik.img", &bd);
	// if (ret) {
	// 	fprintf(stderr, "blockdev_create failed\n");
	// 	return 1;
	// }

	// uint32_t block_count = blockdev_block_count(bd);
	// uint32_t block_size = blockdev_block_size(bd);
	
	// ret = blockdev_destroy(bd);
	// if (ret) {
	// 	fprintf(stderr, "blockdev_destroy failed\n");
	// 	return 1;
	// }

	struct ext2_fs *fs = ext2_init();

	ext2_fs_mount("clik.img", fs, 0);

	ext2_destroy(fs);
	
	return 0;
}
