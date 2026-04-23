#ifndef _EXT2_H_
#define _EXT2_H_

#include <stddef.h>
#include <types.h>
#include <fs/vfs.h>

struct dev_block;

struct vfs *ext2_new(struct vfs_file *dev);
void ext2_destroy(struct vfs *vfs);

#endif
