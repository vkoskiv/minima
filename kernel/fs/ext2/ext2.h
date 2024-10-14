#include <stdint.h>
#include <stdlib.h>

struct ext2_fs;

extern int ext2_errno;

int ext2_fs_mount(const char *img_path, struct ext2_fs *, int flags);
int ext2_fs_umount(struct ext2_fs *);

int ext2_open(struct ext2_fs *fs, const char *pathname, int flags, mode_t mode);
int ext2_close(struct ext2_fs *fs, int fd);

ssize_t ext2_read(struct ext2_fs *fs, int fd, void *buf, size_t count);
ssize_t ext2_write(struct ext2_fs *fs, int fd, const void *buf, size_t count);

char *ext2_getcwd(struct ext2_fs *fs);
int ext2_chdir(struct ext2_fs *fs, const char *path);

int ext2_fsync(struct ext2_fs *fs, int fd);

#define SEEK_SET 1
#define SEEK_CUR 2
#define SEEK_END 4
off_t ext2_lseek(struct ext2_fs *fs, int fd, off_t offset, int whence);

int ext2_creat(struct ext2_fs *fs, const char *pathname, mode_t mode);

int ext2_mkdir(struct ext2_fs *fs, const char *pathname, mode_t mode);

int ext2_rmdir(struct ext2_fs *fs, const char *pathname);

int ext2_link(struct ext2_fs *fs, const char *oldpath, const char *newpath);

int ext2_unlink(struct ext2_fs *fs, const char *pathname);

int ext2_chmod(struct ext2_fs *fs, const char *pathname, mode_t mode);

int ext2_chown(struct ext2_fs *fs, const char *pathname, uid_t owner, gid_t group);

struct dirent {
	// TODO	
};

int ext2_readdir(struct ext2_fs *fs, unsigned int fd, struct dirent *dirp, unsigned int count);

struct utimbuf {
	// TODO
};

int ext2_utime(struct ext2_fs *fs, const char *filename, const struct utimbuf *times);

struct stat {
	// TODO
};

int ext2_stat(struct ext2_fs *fs, const char *pathname, struct stat *statbuf);

