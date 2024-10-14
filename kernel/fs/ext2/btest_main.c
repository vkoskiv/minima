#include <stdio.h>
#include "blockdev.h"

int main(int argc, char **argv) {
	struct blockdev_sim *bd;
	int ret = blockdev_create("clik.img", &bd);
	if (ret) {
		fprintf(stderr, "blockdev_create failed\n");
		return 1;
	}

	uint32_t block_count = blockdev_block_count(bd);
	uint32_t block_size = blockdev_block_size(bd);
	
	ret = blockdev_destroy(bd);
	if (ret) {
		fprintf(stderr, "blockdev_destroy failed\n");
		return 1;
	}
	
	return 0;
}
