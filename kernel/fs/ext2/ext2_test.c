#include "ext2.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <openssl/md5.h>

void pperror(const char *s) {
	// Note: I have no idea when to use which errno, so take these with
	// a grain of salt.
	errno = ext2_errno;
	perror(s);
}

static void dump_md5(char *buf, ssize_t bytes) {
	MD5_CTX c;
	MD5_Init(&c);
	MD5_Update(&c, buf, bytes);
	unsigned char out[MD5_DIGEST_LENGTH];
	MD5_Final(out, &c);
	printf("MD5: ");
	for (int i = 0; i < MD5_DIGEST_LENGTH; ++i)
		printf("%02x", out[i]);
	printf("\n");
}

static void dump_file(struct ext2_fs *fs, const char *path) {
	int fd = ext2_open(fs, path, 0, 0);
	if (fd < 0) {
		printf("ext2_open failed with errno %i\n", ext2_errno);
		return;
	} else {
		printf("ext2_open returned fd %i\n", fd);
	}

	const ssize_t bufsize = 1024 * 1024 * 100; // FIXME
	char *buf = malloc(bufsize);
	ssize_t bytes_read = ext2_read(fs, fd, buf, bufsize);
	if (bytes_read != bufsize) {
		printf("ext2_read returned %i and set the errno to %i\n", bytes_read, ext2_errno);
	}

	dump_md5(buf, bytes_read);

	int ret = ext2_close(fs, fd);
	if (ret) {
		printf("ext2_close failed with errno %i\n", ext2_errno);
	}
	free(buf);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("Usage: %s <filename>\n", argv[0]);
		return 1;
	}
	const char *filename = argv[1];
	struct ext2_fs *fs = ext2_init();
	int ret = ext2_fs_mount(filename, fs, 0);
	if (ret) {
		pperror("ext2_fs_mount");
		return 1;
	}

	dump_file(fs, "/c-ray/src/lib/renderer/pathtrace.c");
	dump_file(fs, "/c-ray/bindings/nodes/convert.py");
	dump_file(fs, "/DOOM.WAD");
	ext2_destroy(fs);
	return 0;
}
