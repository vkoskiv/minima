#ifndef _SLAB_H_
#define _SLAB_H_

#include <stddef.h>

void slab_init(void);

void *slab_alloc(size_t);
void slab_free(void *);

#endif
