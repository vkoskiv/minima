#include <stdint.h>
#include "ext2.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "blockdev.h"
typedef uint32_t time32_t;

int ext2_errno = 0;

#define EXT2_OPT_PREALLOC_BLOCKS 0x0001
#define EXT2_OPT_AFS_INODES      0x0002
#define EXT2_OPT_JOURNAL_PRESENT 0x0004 /* for ext3 */
#define EXT2_OPT_INODE_EXT_ATTR  0x0008
#define EXT2_OPT_FS_RESIZEABLE   0x0010
#define EXT2_OPT_DIR_HASH_IDX    0x0020

#define EXT2_REQ_COMPRESSION     0x0001
#define EXT2_REQ_DIR_TYPE_FIELD  0x0002
#define EXT2_REQ_FS_JRNL_REPLAY  0x0004
#define EXT2_REQ_FS_JRNL_DEVICE  0x0008

#define EXT2_RO_SPARSE_DESC_TBL  0x0001
#define EXT2_RO_FS_64BIT_FSIZE   0x0002
#define EXT2_RO_DIR_BINARY_TREE  0x0004

#define log(...) fprintf(stderr, __VA_ARGS__)

/*
	Always located at byte 1024 from beginning of volume.
	So LBA 2 and 3, assuming 512B sectors
*/
struct ext2_superblock {
	uint32_t inodes_total;
	uint32_t blocks_total;
	uint32_t blocks_reserved_superuser;
	uint32_t blocks_unallocated;
	uint32_t inodes_unallocated;
	uint32_t starting_block; /* Should contain superblock */
	uint32_t block_size; /* log2(blocksize) */
	uint32_t fragment_size; /* log2(frag_size) */

	uint32_t blocks_in_bgroup;
	uint32_t fragms_in_bgroup;
	uint32_t inodes_in_bgroup;

	time32_t last_mounted;
	time32_t last_written;

	uint16_t fsck_n_dirt_mounts;
	uint16_t fsck_n_mounts_allowed;
	uint16_t ext2_signature; /* Always 0xEF53 */
	uint16_t fs_state; /* 1 == clean, 2 == has errors */
	uint16_t error_thing; /* 1 == ignore, 2 == remount ro, 3 == panic */
	uint16_t version_minor;
	time32_t fsck_last_ran;
	time32_t fsck_interval;
	uint32_t os_id; /* 0 = linux, 1 = HURD, 2 = MASIX, 3 = FreeBSD, 4 = *BSD/Darwin */
	uint32_t version_major;
	uint16_t uid_reserved;
	uint16_t gid_reserved;

	// Below fields present if version_major >= 1
	uint32_t first_unreserved_inode;
	uint16_t inode_size;
	uint16_t block_group; /* If backup superblock */
	uint32_t optional_features; /* EXT2_OPT_* */
	uint32_t required_features; /* EXT2_REQ_* */
	uint32_t readonly_features; /* EXT2_RO_* Mount ro if these not present */
	char fs_id[16];
	char vol_name[16];
	char last_mount_path[64];
	uint32_t compression_algs_used;
	uint8_t block_prealloc_file;
	uint8_t block_prealloc_dir;
	uint16_t unused;
	char journal_id[16];
	uint32_t journal_inode;
	uint32_t journal_dev;
	uint32_t orphan_inode_list_head;
	char pad[788];
};

typedef uint32_t blkaddr_t;
typedef uint32_t blkgrp_t;
typedef uint32_t inode_t;

struct block_group_descriptor {
	blkaddr_t block_usage_bitmap;
	blkaddr_t inode_usage_bitmap;
	blkaddr_t inode_table_start;
	uint16_t free_blocks;
	uint16_t free_inodes;
	uint16_t directories;
	char pad[12];
};

struct os_specific {
	uint8_t fragment_num;
	uint8_t fragment_size;
	uint16_t reserved_0;
	uint16_t uid_high;
	uint16_t gid_high;
	uint32_t reserved_1;
};

typedef uint16_t perm_t;

#define ITYPE_FIFO   0x1000
#define ITYPE_CHRDEV 0x2000
#define ITYPE_DIR    0x4000
#define ITYPE_BLKDEV 0x6000
#define ITYPE_REG    0x8000
#define ITYPE_SYMLNK 0xA000
#define ITYPE_SOCKET 0xC000

#define IFLAG_SYNC       0x00000008
#define IFLAG_IMMUTABLE  0x00000010
#define IFLAG_APPENDONLY 0x00000020
// Some more here, TODO

struct inode {
	union {
		// struct bits {
		// 	uint8_t type : 4;
		//  TODO
		// };
		perm_t value;
	} permissions;
	uint16_t uid;
	uint32_t bytes_lsb;
	time32_t last_access;
	time32_t create;
	time32_t last_modify;
	time32_t deleted;
	uint16_t gid;
	uint16_t dir_entries; /* Hard links. When == 0, mark datablocks unallocated */
	uint32_t total_sectors; /* Not counting this structure, nor dir entries */
	uint32_t flags;
	uint32_t os_value;
	blkaddr_t dir_blocks[12];
	blkaddr_t singly_linked; /* blk addr of block of blkaddr_t */
	blkaddr_t doubly_linked; /* blk addr of block of blocks of blkaddr_t */
	blkaddr_t triply_linked; /* blk addr of block of blocks of blocks of blkaddr_t :^) */
	uint32_t generation;
	uint32_t ext_attributes; /* Access control list stuff, if version >= 1 */
	uint32_t bytes_msb; /* I guess if EXT2_RO_FS_64BIT_FSIZE is set? */
	blkaddr_t fragment;
	struct os_specific linux_stuff; /* OS-specific value 2, linux puts some maybe useful stuff in here. TODO. */
};

struct dir_entry {
	inode_t inode;
	uint16_t size;
	uint8_t name_length_lsb;
	uint8_t type; /* If EXT2_REQ_DIR_TYPE_FIELD is set */
	char name[];
};

// For our internal state, I guess?
struct ext2_fs {
	struct blockdev_sim *dev;
	struct ext2_superblock *sb;
};

struct ext2_fs *ext2_init() {
	struct ext2_fs *new = calloc(1, sizeof(*new));
	return new;
}

void ext2_destroy(struct ext2_fs *fs) {
	if (!fs) return;
	if (fs->dev) {
		int ret = blockdev_destroy(fs->dev);
		if (ret)
			log("blockdev_create failed");
	}
	free(fs);
}

int dir_entry_create(inode_t i, const char *name, struct dir_entry **e) {
	if (!name || !*e) return 1;
	ssize_t len = strlen(name);
	struct dir_entry *new = malloc(sizeof(*new) + sizeof(char [strlen(name)]));
	new->inode = i;
	new->size = len + 8;
	new->name_length_lsb = len | 0xFF;
	memcpy(new->name, name, len);
	*e = new;
	return 0;
}

static blkgrp_t bgroup_containing_inode(const struct ext2_superblock *sb, inode_t i) {
	blkgrp_t group = (i - 1) / sb->inodes_in_bgroup;
	// if (group > n_groups) log("group %i > %i\n", group, sb->block_group);
	return group;
}

int ext2_fs_mount(const char *img_path, struct ext2_fs *fs, int flags) {
	if (!img_path || !fs) {
		ext2_errno = -EINVAL;
		return 1;
	}

	int ret = blockdev_create(img_path, &fs->dev);
	if (ret) {
    	log("blockdev_create failed\n");
    	return 1;
	}

	log("Scanning for superblock\n");
	uint32_t blocksize = blockdev_block_size(fs->dev);
	uint32_t block_count = blockdev_block_count(fs->dev);
	struct ext2_superblock *sb = calloc(1, sizeof(*sb));

	for (int i = 0; i < (sizeof(*sb) / blocksize); ++i) {
		log("loading sb block %i\n", i);
		int ret = blockdev_block_read(fs->dev, i + 2, (char *)sb + (i * blocksize));
		if (ret) {
			log("blockdev_block_read failed\n");
			free(sb);
			ext2_errno = -EIO;
			return 1;
		}
	}
	log("Read superblock, validating\n");
	if (sb->ext2_signature != 0xEF53) {
		log("ext2 signature != 0xEF53\n");
		return 1;
	}

	log("Found seemingly okay superblock maybe?\n");

	// struct ext2_superblock sb = find_superblock();
	return 0;
}

int ext2_fs_umount(struct ext2_fs *fs) {
	if (!fs) {
		ext2_errno = -EINVAL;
		return 1;
	}
	int ret = blockdev_destroy(fs->dev);
	if (ret) {
    	log("blockdev_destroy failed\n");
    	return 1;
	}
	fs->dev = NULL;
	return 0;
}

int ext2_open(struct ext2_fs *fs, const char *pathname, int flags, mode_t mode) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}
int ext2_close(struct ext2_fs *fs, int fd) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

ssize_t ext2_read(struct ext2_fs *fs, int fd, void *buf, size_t count) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}
ssize_t ext2_write(struct ext2_fs *fs, int fd, const void *buf, size_t count) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

char *ext2_getcwd(struct ext2_fs *fs) {
	ext2_errno = -EINVAL;
	// TODO
	return NULL;
}
int ext2_chdir(struct ext2_fs *fs, const char *path) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_fsync(struct ext2_fs *fs, int fd) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

off_t ext2_lseek(struct ext2_fs *fs, int fd, off_t offset, int whence) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_creat(struct ext2_fs *fs, const char *pathname, mode_t mode) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_mkdir(struct ext2_fs *fs, const char *pathname, mode_t mode) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_rmdir(struct ext2_fs *fs, const char *pathname) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_link(struct ext2_fs *fs, const char *oldpath, const char *newpath) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_unlink(struct ext2_fs *fs, const char *pathname) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_chmod(struct ext2_fs *fs, const char *pathname, mode_t mode) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_chown(struct ext2_fs *fs, const char *pathname, uid_t owner, gid_t group) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_readdir(struct ext2_fs *fs, unsigned int fd, struct dirent *dirp, unsigned int count) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_utime(struct ext2_fs *fs, const char *filename, const struct utimbuf *times) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_stat(struct ext2_fs *fs, const char *pathname, struct stat *statbuf) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

