#ifndef _MEM_H_
#define _MEM_H_

#include <fs/vfs.h>

const char *memfs_temp_get_name(struct vfs_node *node);
struct vfs *memfs_new(void);

#endif
