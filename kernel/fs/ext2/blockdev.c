/*
	Attempt to write something that resembles a block device
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct blockdev_sim {
	uint16_t block_size;
	uint32_t block_count;
	char *disk;
	char *backing_filepath;
};

int blockdev_create(const char *image_path, struct blockdev_sim **dev) {
	struct stat path_stat = { 0 };
	if (!dev) return 1;
	if (stat(image_path, &path_stat) < 0) {
		perror("stat");
		return 1;
	}
	FILE *fp = fopen(image_path, "rb");
	if (!fp) {
		perror("fopen");
		return 1;
	}
	fseek(fp, 0L, SEEK_END);
	size_t size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);
	if (!size) {
		fprintf(stderr, "Image %s empty\n", image_path);
		fclose(fp);
		return 1;
	}
	if (size % 512 != 0) {
		fprintf(stderr, "image size %% 512 != 0 (%i)\n", size % 512);
		fclose(fp);
		return 1;
	}
	char *data = malloc(size);
	size_t read = fread(data, sizeof(char), size, fp);
	if (read != size) {
		fprintf(stderr, "read (%i) != size (%i)\n", read, size);
		if (ferror(fp) != 0) {
			fprintf(stderr, "error is %s\n", strerror(ferror(fp)));
		}
		fclose(fp);
		free(data);
		return 1;
	}
	fclose(fp);
	struct blockdev_sim *bdev = calloc(1, sizeof(*bdev));
	*bdev = (struct blockdev_sim){
		.block_size = 512,
		.block_count = size / 512,
		.disk = data,
		.backing_filepath = strdup(image_path)
	};
	fprintf(stderr, "blockdev simulating block device, %i blocks, of %i bytes, totaling %i bytes\n",
	        bdev->block_count, bdev->block_size, bdev->block_count * bdev->block_size);
	*dev = bdev;
	return 0;
}

int blockdev_destroy(struct blockdev_sim *bdev) {
	if (!bdev) return 1;
	free(bdev->disk);
	free(bdev);
	return 0;
}

uint32_t blockdev_block_count(struct blockdev_sim *bdev) {
	if (!bdev) return 1;
	return bdev->block_count;
}

uint32_t blockdev_block_size(struct blockdev_sim *bdev) {
	if (!bdev) return 1;
	return bdev->block_size;
}

int blockdev_block_read(struct blockdev_sim *bdev, uint32_t lba, char *out) {
	if (!bdev) return 1;
	if (lba > bdev->block_count) return 1;
	if (!out) return 1;
	memcpy(out, bdev->disk + lba * bdev->block_size, bdev->block_size);
	return 0;
}

int blockdev_block_write(struct blockdev_sim *bdev, uint32_t lba, const char *in) {
	if (!bdev) return 1;
	if (lba > bdev->block_count) return 1;
	if (!in) return 1;
	memcpy(bdev->disk + lba * bdev->block_size, in, bdev->block_size);
	return 0;
}
