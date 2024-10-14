#include <stdint.h>
#include "ext2.h"
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

	uint32_t block_group_blocks;
	uint32_t block_group_fragments;
	uint32_t block_group_inodes;

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
