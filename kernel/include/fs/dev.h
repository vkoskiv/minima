#ifndef _DEV_H_
#define _DEV_H_

#include <fs/vfs.h>
#include <fs/dev_char.h>
#include <fs/dev_block.h>

struct vfs *devfs_get(void);

struct vfs_node *devfs_register_char(struct dev_char *);
struct vfs_node *devfs_register_block(struct dev_block *);
int devfs_unregister(struct device *);

#endif
