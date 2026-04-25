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

struct ext2_node {
	struct vfs_node base;
	struct inode inode;
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

// FIXME: very similar to vfs_file, maybe delete this
struct file {
	struct inode i;
	enum status status;
	uint32_t bytes;
	ssize_t seek_head;
};

// For our internal state, I guess?
struct ext2_fs {
	struct vfs base;
	struct vfs_file *dev;
	ssize_t dev_block_size;
	struct ext2_superblock *sb;
	struct block_group_descriptor *bdesc;
	struct ext2_node *root;
	ssize_t block_size;
	// FIXME: Move to struct task
	uint8_t next_fd;
	struct file open_files[128]; // TODO: dynamic
};

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
	uint32_t disk_block_offset = b * fs->block_size;
	for (ssize_t i = 0; i < (fs->block_size / fs->dev_block_size); ++i) {
		int ret = vfs_read_at(fs->dev, (unsigned char *)(data + (i * fs->dev_block_size)), fs->dev_block_size, disk_block_offset + (i * fs->dev_block_size));
		if (ret) {
			log("ext2: read_block: vfs_read_at: %s\n", strerror(ret));
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
	for (ssize_t ni = 0; ni < name_len; ++ni)
		kput(e->name[ni]);
	kput('\n');
}

static int get_inode(struct ext2_fs *fs, inode_t i, struct inode *out) {
	if (!out)
		return 1;
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

static enum vfs_node_type mode_2_type(mode_t mode) {
	switch (mode & 0xF000) {
	case ITYPE_FIFO  : return nt_fifo;
	case ITYPE_CHRDEV: return nt_dev_char;
	case ITYPE_DIR   : return nt_dir;
	case ITYPE_BLKDEV: return nt_dev_block;
	case ITYPE_REG   : return nt_file;
	case ITYPE_SYMLNK: return nt_symlink;
	case ITYPE_SOCKET: return nt_socket;
	}
	return nt_unknown;
}

struct ext2_node *get_node(struct ext2_fs *fs, inode_t i) {
	struct ext2_node *e2 = kmalloc(sizeof(*e2));
	int ret = get_inode(fs, i, &e2->inode);
	if (ret) {
		kfree(e2);
		kprintf("ext2: get_node: get_inode: %i\n", ret);
		return NULL;
	}
	e2->base.fs = &fs->base;
	e2->base.id = i; // TODO: Not really that useful?
	e2->base.mode = e2->inode.mode; // TODO: Check
	e2->base.type = mode_2_type(e2->inode.mode);
	return e2;
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

static int ext2_mount(struct vfs *vfs) {
	struct ext2_fs *fs = (struct ext2_fs *)vfs;
	struct vfs_stat sb;
	if (!fs->sb)
		return -EINVAL;
	int ret = vfs_stat(fs->dev, &sb);
	if (ret < 0)
		return ret;
	fs->dev_block_size = sb.block_size;
	assert(fs->dev_block_size);
	fs->block_size = (1024 << fs->sb->block_size);

	ssize_t max_descriptors = fs->block_size / sizeof(struct block_group_descriptor);
	struct block_group_descriptor *bdesc = kmalloc(max_descriptors * sizeof(*bdesc));
	// Try to extract block group descriptors next, starting at block 2
	ret = read_block(fs, 2, (char *)bdesc);
	if (ret) {
		kfree(fs->sb);
		return -EIO;
	}
	// TODO: consider moving these checks to find_superblock as well?
	fs->bdesc = bdesc;
	fs->root = get_node(fs, 2);
	if (!fs->root) {
		log("ext2_mount: get_node failed\n");
		kfree(fs->bdesc);
		kfree(fs->sb);
		return -EIO;
	}

	if (!IS_DIR(fs->root->inode.mode)) {
		log("ext2_mount: root inode is not a directory...?\n");
		kfree(fs->bdesc);
		kfree(fs->sb);
		return -EIO;
	}
	return 0;
}

int ext2_fs_umount(struct ext2_fs *fs) {
	if (!fs) {
		ext2_errno = -EINVAL;
		return 1;
	}
	int ret = vfs_close(fs->dev);
	if (ret < 0)
		return ret;
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
			if (dirent->inode == 0)
				continue;
			if (strncmp(dirent->name, name, namelen) == 0) {
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

// void get_dirents_cb(struct ext2_fs *fs, struct dir_entry *dirent, void *ctx) {
// 	struct dir_entry_arr *a = ctx;
// 	if (!fs || !dirent || !ctx) return;

	
// }

// int get_dirents(struct ext2_fs *fs, struct inode inode, struct dir_entry_arr *out) {
// 	if (!fs || !out) return 1;

// 	iterate_dirents(fs, inode, get_dirents_cb, out);
	
// 	return 0;
// }

static struct dir_entry *get_dirent(struct ext2_fs *fs, const char *pathname) {
	if (!fs || !pathname) {
		ext2_errno = -EINVAL;
		return NULL;
	}
	// char *copy = strdup(pathname);
	// char *tok;
	// char *rest = copy;
	struct inode cur = fs->root->inode;
	// char *block = kmalloc(fs->block_size);
	struct dir_entry *dir = NULL;
	v_tok path = v_tok(pathname, '/');
	v_tok part = { 0 };
	while ((part = v_tok_peek(path), !v_tok_empty(part))) {
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

ssize_t ext2_read(struct vfs_file *file, void *buf, size_t bytes);
ssize_t ext2_read_at(struct vfs_file *file, void *buf, size_t bytes, off_t at);

static int ext2_stat(struct vfs_file *file, struct vfs_stat *out) {
	struct ext2_node *n = (void *)file->node;
	struct ext2_fs *fs = (struct ext2_fs *)n->base.fs;
	*out = (struct vfs_stat){
		.size = n->inode.bytes_lsb,
		.block_size = fs->block_size,
		.offset = file->offset,
		.id = n->base.id,
		.mode = n->inode.mode,
		.links = n->inode.dir_entries,
		.uid = n->inode.uid,
		.gid = n->inode.gid,
		.atime = n->inode.last_access,
		.mtime = n->inode.last_modify,
		.ctime = n->inode.create,
	};
	return 0;
}

static const struct vfs_file_ops ext2_file_ops = {
	.read = ext2_read,
	.read_at = ext2_read_at,
	.stat = ext2_stat,
};

int ext2_open(struct vfs_node *node, struct vfs_file **out) {
	struct vfs_file *f = kmalloc(sizeof(*f));
	if (!f)
		return -ENOMEM;
	f->node = node;
	f->offset = 0;
	f->next = NULL;
	f->ops = &ext2_file_ops;
	*out = f;
	return 0;
}

int ext2_close(struct vfs_file *file) {
	kfree(file);
	return 0;
}

ssize_t ext2_read(struct vfs_file *file, void *buf, size_t bytes) {
	struct ext2_fs *fs = (struct ext2_fs *)file->node->fs;
	struct ext2_node *n = (struct ext2_node *)file->node;
	size_t file_bytes = n->inode.bytes_lsb;
	size_t remain = file_bytes - file->offset;
	ssize_t to_read = bytes <= remain ? bytes : remain;
	ssize_t bytes_read = 0;

	// TODO: store this in fs & reuse
	char *block = kmalloc(fs->block_size);

	do {
		ssize_t blk_idx = (bytes_read / fs->block_size);
		// FIXME: pass/return actual amount of bytes from read_i_block
		int ret = read_i_block(fs, &n->inode, blk_idx, block);
		if (ret)
			return ret;
		ssize_t blk_bytes = min((to_read - bytes_read), fs->block_size);
		ssize_t blk_offset = file->offset % fs->block_size;
		memcpy(buf + bytes_read, block + blk_offset, blk_bytes);
		bytes_read += blk_bytes;
	} while (bytes_read < to_read);

	file->offset += bytes_read;
	kfree(block);
	return bytes_read;
}

ssize_t ext2_read_at(struct vfs_file *file, void *buf, size_t bytes, off_t at) {
	struct ext2_fs *fs = (struct ext2_fs *)file->node->fs;
	struct ext2_node *n = (struct ext2_node *)file->node;
	size_t file_bytes = n->inode.bytes_lsb;
	size_t remain = file_bytes - at;
	ssize_t to_read = bytes <= remain ? bytes : remain;
	ssize_t bytes_read = 0;

	// TODO: store this in fs & reuse
	char *block = kmalloc(fs->block_size);

	do {
		ssize_t blk_idx = (bytes_read / fs->block_size);
		// FIXME: pass/return actual amount of bytes from read_i_block
		int ret = read_i_block(fs, &n->inode, blk_idx, block);
		if (ret)
			return ret;
		ssize_t blk_bytes = min((to_read - bytes_read), fs->block_size);
		ssize_t blk_offset = at % fs->block_size;
		memcpy(buf + bytes_read, block + blk_offset, blk_bytes);
		bytes_read += blk_bytes;
	} while (bytes_read < to_read);

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

int ext2_utime(struct ext2_fs *fs, const char *filename, const struct utimbuf *times) {
	ext2_errno = -EINVAL;
	// TODO
	return 1;
}

int ext2_unmount(struct vfs *fs) {
	return -ENOSYS;
}

struct lookup_ctx {
	const char *name;
	struct vfs_node **out;
};

void lookup_cb(struct ext2_fs *fs, struct dir_entry *e, void *ctx) {
	struct lookup_ctx *c = ctx;
	ssize_t name_len = 0;
	if (fs->sb->required_features & EXT2_REQ_DIR_TYPE_FIELD) {
		name_len = e->name_length_lsb;
	} else {
		name_len = (e->type << 8) | e->name_length_lsb;
	}
	if (strncmp(c->name, e->name, name_len))
		return;
	*c->out = &get_node(fs, e->inode)->base;
}

int ext2_lookup(struct vfs_node *dir, const char *name, struct vfs_node **out) {
	struct ext2_node *d = (struct ext2_node *)dir;
	struct ext2_fs *fs = (struct ext2_fs *)d->base.fs;
	if (strlen(name) == 1 && *name == '.') {
		*out = &d->base;
		return 0;
	}
	if (d == fs->root && strlen(name) == 2 && strncmp(name, "..", 2) == 0) {
		*out = fs->base.parent_node;
		return 0;
	}
	struct vfs_node *temp = NULL;
	struct lookup_ctx ctx = {
		.name = name,
		.out = &temp,
	};
	iterate_dirents((struct ext2_fs *)dir->fs, d->inode, lookup_cb, &ctx);
	if (!temp)
		return -ENOENT;
	*out = temp;
	return 0;
}

struct readdir_ctx {
	size_t idx;
	size_t iter;
	char **name_out;
};

void readdir_cb(struct ext2_fs *fs, struct dir_entry *e, void *ctx) {
	struct readdir_ctx *c = ctx;
	if (c->iter++ != c->idx)
		return;
	ssize_t name_len = 0;
	if (fs->sb->required_features & EXT2_REQ_DIR_TYPE_FIELD) {
		name_len = e->name_length_lsb;
	} else {
		name_len = (e->type << 8) | e->name_length_lsb;
	}
	*c->name_out = kmalloc(name_len + 1);
	memcpy(*c->name_out, e->name, name_len);
	(*c->name_out)[name_len] = 0;
	// kprintf("readdir_cb name_len: %u, name: '%s'\n", name_len, e->name);
	// FIXME: return value to stop this iteration.
}

int ext2_readdir(struct vfs_node *dir, size_t idx, char **name_out) {
	struct ext2_node *node = (struct ext2_node *)dir;
	char *temp = NULL;
	struct readdir_ctx ctx = {
		.idx = idx,
		.iter = 0,
		.name_out = &temp,
	};
	iterate_dirents((struct ext2_fs *)dir->fs, node->inode, readdir_cb, &ctx);
	if (!temp)
		return -ENOENT;
	if (*name_out)
		*name_out = temp;
	return 0;
}

int ext2_create(struct vfs_node *dir, const char *name, mode_t mode) {
	return 0;
}

// int ext2_mkdir(struct ext2_fs *fs, const char *pathname, mode_t mode);
int ext2_mkdir(struct vfs_node *dir, const char *name, mode_t mode) {
	return 0;
}

struct vfs_node *ext2_get_root(struct vfs *vfs) {
	struct ext2_fs *fs = (struct ext2_fs *)vfs;
	return &fs->root->base;
}

static const struct vfs_ops ext2_ops = {
	.mount = ext2_mount,
	.unmount = ext2_unmount,
	.get_root = ext2_get_root,
	.lookup = ext2_lookup,
	.create = NULL,
	.mkdir = NULL,
	.open = ext2_open,
	.close = ext2_close,
	.readdir = ext2_readdir,
};

static struct ext2_superblock *find_superblock(struct vfs_file *dev) {
	if (!dev)
		return NULL;
	struct ext2_superblock super = { 0 };
	struct vfs_stat sb;
	int ret = vfs_stat(dev, &sb);
	if (ret < 0) {
		kprintf("ext2: find_superblock: %s\n", strerror(ret));
		return NULL;
	}

	// TODO: make superblock detection iterate device to find sb
	for (size_t i = 0; i < (sizeof(super) / sb.block_size); ++i) {
		ret = vfs_read_at(dev, ((unsigned char *)&super) + (i * sb.block_size), sb.block_size, (i + 2) * sb.block_size);
		if (ret) {
			log("ext2: find_superblock: vfs_read_at() failed to read superblock, ret = %i (%s)\n", ret, strerror(ret));
			return NULL;
		}
	}
	if (super.ext2_signature != 0xEF53) {
		// log("ext2: find_superblock: signature %2h != 0xEF53\n", super.ext2_signature);
		return NULL;
	}
	ssize_t fs_block_size = 1024 << super.block_size;
	uint32_t fs_bytes = super.blocks_total * fs_block_size;
	if (fs_bytes != sb.size) {
		kprintf("ext2: find_superblock: ext2 size %u != device size %u\n", fs_bytes, sb.size);
		return NULL;
	}

	// Figure out block group amount multiple different ways and cross-check
	ssize_t block_groups_0 = (super.inodes_total + super.inodes_per_bgroup - 1) / super.inodes_per_bgroup;
	ssize_t block_groups_1 = (super.blocks_total + super.blocks_per_bgroup - 1) / super.blocks_per_bgroup;
	if (block_groups_0 != block_groups_1) {
		log("ext2: find_superblock: inodes/inodes_per_bgroup != blocks / blocks_per_bgroup, something's busted\n");
		return NULL;
	}

	struct ext2_superblock *out = kmalloc(sizeof(*out));
	*out = super;
	return out;
}

struct vfs *ext2_new(struct vfs_file *dev) {
	if (!dev)
		return NULL;
	struct ext2_superblock *super = find_superblock(dev);
	if (!super)
		return NULL;
	struct ext2_fs *fs = kzalloc(sizeof(*fs));
	fs->dev = dev;
	fs->sb = super;
	fs->base.name = "ext2";
	fs->base.parent = NULL;
	fs->base.parent_node = NULL;
	fs->base.ops = &ext2_ops;
	return &fs->base;
}

void ext2_destroy(struct vfs *vfs) {
	struct ext2_fs *fs = (void *)vfs;
	if (!fs)
		return;
	if (fs->bdesc)
		kfree(fs->bdesc);
	if (fs->sb)
		kfree(fs->sb);
	if (fs->dev)
		vfs_close(fs->dev);
	kfree(fs);
}
