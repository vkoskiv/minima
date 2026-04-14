#ifndef _VFS_H_
#define _VFS_H_

#include <fs/dev_block.h>
#include <types.h>

struct vfs;
struct vfs_node;
struct vfs_file;

struct vfs_ops {
	int (*mount)  (struct vfs *fs, void *dev);
	int (*unmount)(struct vfs *fs);
	int (*lookup) (struct vfs_node *dir, const char *name, struct vfs_node **out);
	int (*readdir)(struct vfs_node *dir, size_t idx, char **name_out);
	int (*open)   (struct vfs_node *node, struct vfs_file **out);
	int (*close)  (struct vfs_file *file);
	int (*create) (struct vfs_node *dir, const char *name, mode_t mode);
	int (*mkdir)  (struct vfs_node *dir, const char *name, mode_t mode);
	struct vfs_node *(*get_root)(struct vfs *fs);
};

struct vfs {
	const char *name;
	struct vfs_node *parent;
	const struct vfs_ops *ops;
};

enum vfs_node_type {
	nt_unknown = 0,
	nt_file,
	nt_dir,
	nt_dev_char,
	nt_dev_block,
	nt_fifo,
	nt_socket,
	nt_symlink,
	// nt_mountpoint, ?
};

extern const char vfs_node_type_chars[nt_symlink + 1];

struct vfs_node {
	struct vfs *fs;
	uint32_t id;
	enum vfs_node_type type;
	mode_t mode; // FIXME: Not sure if this is a good place to keep this
};

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct vfs_file_ops {
	ssize_t (*read) (struct vfs_file *file, void *buf, size_t bytes);
	ssize_t (*write)(struct vfs_file *file, const void *buf, size_t bytes);
	off_t   (*seek) (struct vfs_file *file, off_t offset, int mode);
};

struct vfs_file {
	struct vfs_node *node;
	uint32_t offset;
	const struct vfs_file_ops *ops;
	struct vfs_file *next;
};

struct vfs_stat {
	size_t size;
	off_t offset;
	uint32_t id;
	mode_t mode;
	uint32_t links;
	uid_t uid;
	gid_t gid;
	uint32_t atime;
	uint32_t mtime;
	uint32_t ctime;
};

// --- FS ops ---
int vfs_mount(struct vfs *fs, const char *mountpoint);
int vfs_unmount(struct vfs *fs);
int vfs_lookup(struct vfs_node *dir, const char *name, struct vfs_node **out);
int vfs_readdir(struct vfs_node *dir, size_t idx, char **name_out);
int vfs_open(struct vfs_node *node, struct vfs_file **out);
int vfs_close(struct vfs_file *file);
int vfs_create(struct vfs_node *dir, const char *name, mode_t mode);
int vfs_mkdir(struct vfs_node *dir, const char *name, mode_t mode);
struct vfs_node *vfs_get_root(void);

// --- File ops ---
ssize_t vfs_read(struct vfs_file *file, void *buf, size_t bytes);
ssize_t vfs_write(struct vfs_file *file, const void *buf, size_t bytes);
off_t vfs_seek(struct vfs_file *file, off_t offset, int mode);
int vfs_stat(struct vfs_file *file, struct vfs_stat *out);

// --- Higher-level VFS abstractions ---

int vfs_traverse(struct vfs_node *from, const char *path, struct vfs_node **out);
int vfs_chdir(const char *path);
struct vfs_node *vfs_get_cwd(void);

#endif
