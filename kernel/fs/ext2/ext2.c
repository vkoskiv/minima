// #include <sys/param.h>
// #include <math.h>
#include <stdint.h>
// #include <time.h>
#include <fs/ext2/ext2.h>
#include <errno.h>
#include <fs/dev_block.h>
#include <kprintf.h>
#include <vkern.h>
#include <utils.h>
#include <fs/dev_block.h>
#include <kmalloc.h>
#include <assert.h>
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

#define log(...) kprintf(__VA_ARGS__)

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

	uint32_t blocks_per_bgroup;
	uint32_t fragms_per_bgroup;
	uint32_t inodes_per_bgroup;

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
typedef uint32_t blk_idx_t;
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
	perm_t mode;
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

enum status {
	F_CLOSED = 0,
	F_OPENED,
};

struct file {
	struct inode i;
	enum status status;
	uint32_t bytes;
	ssize_t seek_head;
};

// For our internal state, I guess?
struct ext2_fs {
	struct dev_block *dev;
	struct ext2_superblock *sb;
	struct block_group_descriptor *bdesc;
	struct inode root;
	ssize_t block_size;
	uint8_t next_fd;
	struct file open_files[128]; // TODO: dynamic
};

// FIXME: move to mount
struct ext2_fs *ext2_init() {
	struct ext2_fs *new = kzalloc(sizeof(*new));
	return new;
}

void ext2_destroy(struct ext2_fs *fs) {
	if (!fs)
		return;
	if (fs->bdesc)
		kfree(fs->bdesc);
	if (fs->sb)
		kfree(fs->sb);
	if (fs->dev) {
		dev_block_close(fs->dev);
	}
	kfree(fs);
}

static void print_unixtime(const char *prefix, time_t t) {
	// FIXME
	// const char *format = "%c";
	// struct tm lt;
	// char res[32];
	// localtime_r(&t, &lt);
	// strftime(res, sizeof(res), format, &lt);
	// log("%s%s", prefix, res);
	kprintf("%s %u", prefix, t);
}

int dir_entry_create(inode_t i, const char *name, struct dir_entry **e) {
	if (!name || !*e) return 1;
	ssize_t len = strlen(name);
	struct dir_entry *new = kmalloc(sizeof(*new) + sizeof(char [strlen(name)]));
	new->inode = i;
	new->size = len + 8;
	// new->name_length_lsb = len | 0xFF;
	memcpy(new->name, (void *)name, len);
	*e = new;
	return 0;
}

static int read_block(struct ext2_fs *fs, blkaddr_t b, char *data) {
	if (!data) return 1;
	uint32_t disk_blocksize = dev_block_get_block_size(fs->dev);
	uint32_t disk_block_offset = (b * fs->block_size) / disk_blocksize;
	for (size_t i = 0; i < (fs->block_size / disk_blocksize); ++i) {
		int ret = dev_block_read(fs->dev, disk_block_offset + i, (unsigned char *)(data + (i * disk_blocksize)));
		if (ret) {
			log("read_block: blockdev_block_read failed\n");
			ext2_errno = -ENODATA;
			return 1;
		}
	}

	return 0;
}

static int read_indirect(struct ext2_fs *fs, struct inode *i, blk_idx_t idx, char *data, blkaddr_t *cur, int depth) {
	if (!fs || !i || !data) return 1;
	const ssize_t apb = fs->block_size / sizeof(blkaddr_t); // addresses per block
	ssize_t cur_idx = 0;
	switch (depth) {
	case 0: cur_idx = idx % apb; break;
	case 1: cur_idx = (idx / apb) % apb; break;
	case 2: cur_idx = (idx / (apb*apb)) % (apb*apb); break;
	}
	if (depth == 0) {
		if (!cur[cur_idx]) {
			log("read_indirect blkptr == 0 at index %i, depth %i\n", cur_idx, depth);
			ext2_errno = -ENODATA;
			return 1;
		}
		int ret = read_block(fs, cur[cur_idx], data);
		if (ret) {
			log("read_indirect failed\n");
			ext2_errno = -ENODATA;
			return 1;
		}
		return 0;
	}

	char *next_block = kmalloc(fs->block_size);
	int ret = read_block(fs, cur[cur_idx], next_block);
	if (ret) {
		log("read_indirect traverse read_block failed\n");
		ext2_errno = -ENODATA;
		kfree(next_block);
		return 1;
	}

	ret = read_indirect(fs, i, idx, data, (blkaddr_t *)next_block, depth - 1);
	kfree(next_block);
	return ret;
}

// Read 'absolute' block of an inode, traversing list tree if necessary.
// idx == 0 => first block of inode i
// idx == (i.bytes_lsb / fs->block_size) - 1 => last block of inode i
static int read_i_block(struct ext2_fs *fs, struct inode *i, blk_idx_t idx, char *data) {
	if (!fs || !i || !data) return 1;
	if (idx < 12 && i->dir_blocks[idx])
		return read_block(fs, i->dir_blocks[idx], data);
	const size_t apb = fs->block_size / sizeof(blkaddr_t);
	idx -= 12;
	int ret = 0;
	// FIXME: before changing to kmalloc(), each if block had its own
	// block allocation. Not sure how important that is.
	char *block = kmalloc(fs->block_size);
	if (idx < apb && i->singly_linked) {
		ret = read_block(fs, i->singly_linked, block);
		if (ret)
			goto out;
		ret = read_indirect(fs, i, idx, data, (blkaddr_t *)block, 0);
		goto out;
	}
	idx -= apb;
	if (idx < (apb * apb) && i->doubly_linked) {
		ret = read_block(fs, i->doubly_linked, block);
		if (ret)
			goto out;
		ret = read_indirect(fs, i, idx, data, (blkaddr_t *)block, 1);
		goto out;
	}
	idx -= (apb * apb);
	if (idx < (apb * apb * apb) && i->triply_linked) {
		ret = read_block(fs, i->triply_linked, block);
		if (ret)
			goto out;
		ret = read_indirect(fs, i, idx, data, (blkaddr_t *)block, 2);
		goto out;
	}
	log("Index %i greater addr_per_block ^ 3, which should not happen.\n", idx);
	kfree(block);
	return 1;
out:
	kfree(block);
	return ret;
}

#define IS_FIF(m) ((m & 0xF000) == ITYPE_FIFO)
#define IS_CHR(m) ((m & 0xF000) == ITYPE_CHRDEV)
#define IS_DIR(m) ((m & 0xF000) == ITYPE_DIR)
#define IS_BLK(m) ((m & 0xF000) == ITYPE_BLKDEV)
#define IS_REG(m) ((m & 0xF000) == ITYPE_REG)
#define IS_LNK(m) ((m & 0xF000) == ITYPE_SYMLNK)
#define IS_SKT(m) ((m & 0xF000) == ITYPE_SOCKET)
/*

    -: “regular” file, created with any program which can write a file
    b: block special file, typically disk or partition devices, can be created with mknod
    c: character special file, can also be created with mknod (see /dev for examples)
    d: directory, can be created with mkdir
    l: symbolic link, can be created with ln -s
    p: named pipe, can be created with mkfifo
    s: socket, can be created with nc -U
    D: door, created by some server processes on Solaris/openindiana.

*/
static void dump_permissions(perm_t p) {
	char buf[10];
	switch (p & 0xF000) {
	case 0x1000: buf[0] = 'p'; break; // FIFO/named pipe
	case 0x2000: buf[0] = 'c'; break; // Character device
	case 0x4000: buf[0] = 'd'; break; // Directory
	case 0x6000: buf[0] = 'b'; break; // Block device
	case 0x8000: buf[0] = '-'; break; // Regular file
	case 0xA000: buf[0] = 'l'; break; // Symlink
	case 0xC000: buf[0] = 's'; break; // Unix local-domain socket
	}
	p <<= 4;
	char setuid = p & 0x8000 ? 1 : 0; p <<= 1;
	char setgid = p & 0x8000 ? 1 : 0; p <<= 1;
	char sticky = p & 0x8000 ? 1 : 0; p <<= 1;
	
	const char *c = "rwx";
	for (int i = 0; i < 9; ++i) {
		char next = c[i % 3];
		switch (i) {
		case 2: next = setuid ? 's' : next; break; // setuid
		case 5: next = setgid ? 's' : next; break; // setgid
		case 8: next = sticky ? 't' : next; break; // sticky
		}
		buf[1 + i] = p & 0x8000 ? next : '-';
		p <<= 1;
	}
	// FIXME: kprintf
	for (size_t i = 0; i < 10; ++i)
		kput(buf[i]);
	// log("%.*s", 10, buf);
}

void dump_dirent(struct ext2_fs *fs, struct dir_entry *e, struct inode *i) {
	// print_unixtime("inode last_access: ", i->last_access); log("\n");
	// print_unixtime("inode create: ", i->create); log("\n");
	// print_unixtime("inode last_modify: ", i->last_modify); log("\n");
	// print_unixtime("inode deleted: ", i->deleted); log("\n");
	// log("inode total_sectors: %i\n", i->total_sectors);
	dump_permissions(i->mode);
	log(" %i ", i->dir_entries);
	log(" %s ", i->uid ? "anon" : "root");
	log(" %s ", i->gid ? "anon" : "root");
	// FIXME
	// if (fs->sb->readonly_features & EXT2_RO_FS_64BIT_FSIZE) {
	// 	uint64_t size = ((uint64_t)i->bytes_msb << 32) | i->bytes_lsb;
	// 	log(" %10llu ", size);
	// } else {
	// 
		// log(" %10i ", i->bytes_lsb); FIXME: kprintf padding
		log(" %i ", i->bytes_lsb);
	// }
	print_unixtime("", i->last_modify);
	ssize_t name_len = 0;
	if (fs->sb->required_features & EXT2_REQ_DIR_TYPE_FIELD) {
		name_len = e->name_length_lsb;
	} else {
		name_len = (e->type << 8) | e->name_length_lsb;
	}
	// log( " %.*s\n", name_len, e->name);
	// FIXME: kprintf
	kput(' ');
	for (ssize_t i = 0; i < name_len; ++i)
		kput(e->name[i]);
	kput('\n');
}

static int get_inode(struct ext2_fs *fs, inode_t i, struct inode *out) {
	if (!out) return 1;
	blkgrp_t group = (i - 1) / fs->sb->inodes_per_bgroup;
	blkaddr_t inode_table_start = fs->bdesc[group].inode_table_start;
	ssize_t i_idx = (i - 1) % fs->sb->inodes_per_bgroup;
	blkaddr_t inode_blk = (i_idx * fs->sb->inode_size) / (fs->block_size);
	// log("grp: %i, tblstart: %i, i_idx: %i, inode_blk: %i\n", group, inode_table_start, i_idx, inode_blk);
	// read block at inode_table_start + inode_blk
	char *block = kmalloc(fs->block_size);
	int ret = read_block(fs, inode_table_start + inode_blk, block);
	if (ret) {
		log("get_inode: read_block failed");
		kfree(block);
		return 1;
	}
	const ssize_t inodes_per_block = (fs->block_size / fs->sb->inode_size);
	ssize_t inode_offset = i_idx % inodes_per_block;
	// Now try to extract the actual inode
	memcpy(out, (block + (inode_offset * fs->sb->inode_size)), sizeof(*out));
	
	kfree(block);
	return 0;
}

static void iterate_dirents(struct ext2_fs *fs, struct inode inode, void (*cb)(struct ext2_fs *, struct dir_entry *, void *), void *ctx);
const char *direntry_types[] = {
	"????",
	"REG",
	"DIR",
	"CHR",
	"BLK",
	"FIFO",
	"SCKT",
	"SLNK",
};

static inline void indent(int n) {
	if (!n) return;
    for (int i = 0; i < n; i++) kput('\t');
}

int dump_recursive(struct ext2_fs *fs, struct inode cur, int depth);

static void dump_recursive_cb(struct ext2_fs *fs, struct dir_entry *dirent, void *ctx) {
	struct inode i = { 0 };
	int ret = get_inode(fs, dirent->inode, &i);
	if (ret) return;
	int depth = *(int *)ctx;
	indent(depth); dump_dirent(fs, dirent, &i);
	if (dirent->type != 2) return;

	ssize_t name_len = 0;
	if (fs->sb->required_features & EXT2_REQ_DIR_TYPE_FIELD) {
		name_len = dirent->name_length_lsb;
	} else {
		name_len = (dirent->type << 8) | dirent->name_length_lsb;
	}
	if (name_len && dirent->name[0] == '.' && dirent->name[1] == '.') return;
	if (name_len == 1 && dirent->name[0] == '.') return;
	dump_recursive(fs, i, depth + 1);
}

int dump_recursive(struct ext2_fs *fs, struct inode cur, int depth) {
	iterate_dirents(fs, cur, dump_recursive_cb, &depth);
	return 0;
}

int ext2_fs_mount(const char *img_path, struct ext2_fs *fs, int flags) {
	if (!img_path || !fs) {
		ext2_errno = -EINVAL;
		return 1;
	}

	struct dev_block *dev = dev_block_open(img_path);
	if (!dev)
		return -ENODEV;
	fs->dev = dev;

	log("Scanning for superblock\n");
	uint32_t blocksize = dev_block_get_block_size(fs->dev);
	uint32_t block_count = dev_block_get_block_count(fs->dev);
	fs->sb = kzalloc(sizeof(*fs->sb));
	// FIXME: use read_block here too, and maybe name it ext2_block_read or something
	for (size_t i = 0; i < (sizeof(*fs->sb) / blocksize); ++i) {
		int ret = dev_block_read(fs->dev, i + 2, (unsigned char *)fs->sb + (i * blocksize));
		if (ret) {
			log("blockdev_block_read failed\n");
			kfree(fs->sb);
			ext2_errno = -EIO;
			return 1;
		}
	}
	if (fs->sb->ext2_signature != 0xEF53) {
		log("ext2 signature != 0xEF53\n");
		return 1;
	}
	fs->block_size = (1024 << fs->sb->block_size);
	if ((fs->sb->blocks_total / fs->block_size) != (block_count / blocksize))
		log("ext2 blocks != blockdev blocks\n");


	log("Valid superblock signature. Some info:\n");
	log("inodes: %i\n", fs->sb->inodes_total);
	log("blocks: %i\n", fs->sb->blocks_total);
	log("block_count: %i\n", block_count);
	log("inodes_per_bgroup: %i\n", fs->sb->inodes_per_bgroup);
	log("starting_block: %i\n", fs->sb->starting_block);
	log("blocksize: %i\n", fs->block_size);
	log("blocks_per_bgroup: %i\n", fs->sb->blocks_per_bgroup);
	log("fragsize: %i\n", 1024 << fs->sb->fragment_size);
	log("first_unreserved_inode: %i\n", fs->sb->first_unreserved_inode);
	log("inode_size: %i\n", fs->sb->inode_size);

	// Figure out block group amount multiple different ways and cross-check
	ssize_t block_groups_0 = (fs->sb->inodes_total + fs->sb->inodes_per_bgroup - 1) / fs->sb->inodes_per_bgroup;
	ssize_t block_groups_1 = (fs->sb->blocks_total + fs->sb->blocks_per_bgroup - 1) / fs->sb->blocks_per_bgroup;
	if (block_groups_0 != block_groups_1) {
		log("inodes/inodes_per_bgroup != blocks / blocks_per_bgroup, something's busted\n");
		return 1;
	}

	print_unixtime("Last mounted: ", fs->sb->last_mounted); log("\n");
	print_unixtime("Last written: ", fs->sb->last_written); log("\n");

	ssize_t max_descriptors = fs->block_size / sizeof(struct block_group_descriptor);
	struct block_group_descriptor *bdesc = kmalloc(max_descriptors * sizeof(*bdesc));
	// Try to extract block group descriptors next, starting at block 2
	log("Loading max. %i descriptors\n", max_descriptors);
	int ret = read_block(fs, 2, (char *)bdesc);
	if (ret) {
		kfree(fs->sb);
		ext2_errno = -EIO;
		return 1;
	}
	fs->bdesc = bdesc;
	// And iterate
	for (int i = 0; i < max_descriptors; ++i) {
		struct block_group_descriptor b = bdesc[i];
		if (!b.free_blocks) continue;
		log("block_group_descriptor %i:\n", i);
		log("\tblock_usage_bitmap: %i\n", b.block_usage_bitmap);
		log("\tinode_usage_bitmap: %i\n", b.inode_usage_bitmap);
		log("\tinode_table_start: %i\n", b.inode_table_start);
		ssize_t blocks_used = fs->sb->blocks_per_bgroup - b.free_blocks;
		log("\tfree_blocks: %i (%i used = %ukB)\n", b.free_blocks, blocks_used, (blocks_used * fs->block_size) / 1024);
		log("\tfree_inodes: %i (%i used)\n", b.free_inodes, fs->sb->inodes_per_bgroup - b.free_inodes);
		log("\tdirectories: %i\n", b.directories);
	}

	struct inode root_inode = { 0 }; /* inode 2 */
	ret = get_inode(fs, 2, &root_inode);
	if (ret) {
		log("get_inode failed for inode %i\n", 2);
		return 1;
	}
	fs->root = root_inode;

	// Try to iterate directory entries
	if (!IS_DIR(root_inode.mode)) {
		log("root_inode is not a directory...?\n");
		return 1;
	}

	dump_recursive(fs, root_inode, 0);
	
	return 0;
}

int ext2_fs_umount(struct ext2_fs *fs) {
	if (!fs) {
		ext2_errno = -EINVAL;
		return 1;
	}
	dev_block_close(fs->dev);
	// if (ret) {
 //    	log("blockdev_destroy failed\n");
 //    	return 1;
	// }
	fs->dev = NULL;
	return 0;
}

static void iterate_dirents(struct ext2_fs *fs, struct inode inode, void (*cb)(struct ext2_fs *, struct dir_entry *, void *), void *ctx) {
	size_t i = 0;
	char *cur_blk = kmalloc(fs->block_size);
	// First iterate direct blocks
	while (inode.dir_blocks[i]) {
		int ret = read_block(fs, inode.dir_blocks[i], cur_blk);
		if (ret) break; // FIXME: error handling much?
		ssize_t blk_offset = 0;
		while (blk_offset < fs->block_size) {
			struct dir_entry *dirent = (void *)&cur_blk[blk_offset];
			blk_offset += dirent->size ? dirent->size : fs->block_size;
			if (!dirent->inode)
				continue;
			cb(fs, dirent, ctx);
		}
		i++;
	}
	if (i == 11) log("maybe need to iterate indirect blocks\n");
	// TODO: indirect
	// TODO: doubly indirect
	// TODO: triply indirect
	kfree(cur_blk);
}

static struct dir_entry *find_dirent(struct ext2_fs *fs, struct inode cur, const char *name, size_t namelen) {
	if (!fs || !name) {
		ext2_errno = -EINVAL;
		return NULL;
	}
	int i = 0;
	char *cur_blk = kmalloc(fs->block_size);
	while (cur.dir_blocks[i]) {
		int ret = read_block(fs, cur.dir_blocks[i], cur_blk);
		if (ret)
			break;
		ssize_t blk_offset = 0;
		while (blk_offset < fs->block_size) {
			struct dir_entry *dirent = (struct dir_entry *)(cur_blk + blk_offset);
			if (dirent->inode == 0) continue;
			if (strcmp(dirent->name, name) == 0) {
				struct dir_entry *found = kmalloc(dirent->size);
				memcpy(found, dirent, dirent->size);
				kfree(cur_blk);
				return found;
			}
			blk_offset += dirent->size ? dirent->size : fs->block_size;
		}
		i++;
	}
	kfree(cur_blk);
	return NULL;
}

void get_dirents_cb(struct ext2_fs *fs, struct dir_entry *dirent, void *ctx) {
	struct dir_entry_arr *a = ctx;
	if (!fs || !dirent || !ctx) return;

	
}

int get_dirents(struct ext2_fs *fs, struct inode inode, struct dir_entry_arr *out) {
	if (!fs || !out) return 1;

	iterate_dirents(fs, inode, get_dirents_cb, out);
	
	return 0;
}

static struct dir_entry *get_dirent(struct ext2_fs *fs, const char *pathname) {
	if (!fs || !pathname) {
		ext2_errno = -EINVAL;
		return NULL;
	}
	// char *copy = strdup(pathname);
	// char *tok;
	// char *rest = copy;
	struct inode cur = fs->root;
	// char *block = kmalloc(fs->block_size);
	struct dir_entry *dir = NULL;
	v_tok path = v_tok(pathname, '/');
	v_tok part = { 0 };
	while ((part = v_tok_peek(path), !v_tok_empty(path))) {
		for (size_t i = 0; i < v_tok_len(part); ++i)
			kput(part.beg[i]);
		kprintf("'\n");
		dir = find_dirent(fs, cur, part.beg, v_tok_len(part));
		if (!dir) {
			log("Failed to find dirent for %s\n", pathname);
			goto err;
		}
		int ret = get_inode(fs, dir->inode, &cur);
		if (ret)
			goto err;
		else
			return dir;
	}
	// while ((tok = strtok_r(rest, "/", &rest))) {
	// 	dir = find_dirent(fs, cur, tok);
	// 	if (!dir) {
	// 		log("Failed to find dirent for %s\n", tok);
	// 		goto err;
	// 	}
	// 	int ret = get_inode(fs, dir->inode, &cur);
	// 	if (ret) goto err;
	// }
	// kfree(copy);
	ext2_errno = 0;
	return dir;
err:
	ext2_errno = -ENOENT;
	return NULL;
}

int ext2_open(struct ext2_fs *fs, const char *pathname, int flags, mode_t mode) {
	log("ext2_open(\"%s\")\n", pathname);
	if (!fs || !pathname) {
		ext2_errno = -EINVAL;
		return 1;
	}
	struct file f = { 0 };
	struct dir_entry *dir = get_dirent(fs, pathname);
	if (!dir) {
		log("get_dirent failed\n");
		ext2_errno = -EINVAL;
		return -1;
	}
	int ret = get_inode(fs, dir->inode, &f.i);
	kfree(dir);
	if (ret) {
		log("ext2_open -> get_inode failed, errno %i\n", ext2_errno);
		return -1;
	}
	int fd = fs->next_fd++;
	f.bytes = f.i.bytes_lsb; // TODO: 64 bit?
	if (f.i.bytes_msb) {
		log("FIXME: File %s uses bytes_msb, which we're ignoring\n", pathname);
	}
	f.status = F_OPENED;
	fs->open_files[fd] = f;
	return fd;
}

int ext2_close(struct ext2_fs *fs, int fd) {
	if (!fs || fd < 0) {
		ext2_errno = -EINVAL;
		return 1;
	}
	struct file *f = &fs->open_files[fd];
	if (f->status != F_OPENED) {
		ext2_errno = -EINVAL;
		return 1;
	}
	memset(f, 0, sizeof(*f));
	return 0;
}

ssize_t ext2_read(struct ext2_fs *fs, int fd, void *buf, size_t count) {
	if (!fs || !buf) {
		ext2_errno = -EINVAL;
		return 1;
	}
	ssize_t bytes_read = 0;
	if (!count) return bytes_read;	
	struct file *f = &fs->open_files[fd];
	if (f->status != F_OPENED) {
		ext2_errno = -EBADF;
		return 0;
	}
	if (IS_DIR(f->i.mode)) {
		ext2_errno = -EISDIR;
		return 0;
	}
	if (!IS_REG(f->i.mode)) {
		ext2_errno = -EINVAL;
		return 0;
	}
	ssize_t total_to_read = count;
	if (count > f->bytes)
		total_to_read = f->bytes;

	// char *cur_block = kmalloc(fs->block_size);
	log("Trying to read %i bytes\n", total_to_read);
	// if (total_to_read > (12 * fs->block_size)) {
	// 	log("FIXME: File is over 12 blocks and we don't support indirect blocks yet. Truncating.\n");
	// 	total_to_read = 12 * fs->block_size;
	// }

	char *block = kmalloc(fs->block_size); // FIXME: read_i_block directly to buf
	do {
		ssize_t blk_idx = (bytes_read / fs->block_size);
		// int ret = read_block(fs, f->i.dir_blocks[blk_idx], block);
		int ret = read_i_block(fs, &f->i, blk_idx, block);
		if (ret)
			break;
		ssize_t blk_bytes = min((total_to_read - bytes_read), fs->block_size);
		// log("Read %i bytes from iblock %i\n", blk_bytes, blk_idx);
		memcpy(buf + bytes_read, block, blk_bytes);
		bytes_read += blk_bytes;
	} while (bytes_read < total_to_read);
	log("Read %i bytes successfully\n", bytes_read);
	kfree(block);
	return bytes_read;
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

