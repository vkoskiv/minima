//
// kmalloc.h - generic memory allocator
//

#ifndef _KMALLOC_H_
#define _KMALLOC_H_

#include <stddef.h>

void *kmalloc(size_t bytes);
void kfree(void *ptr);

#endif
