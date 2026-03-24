//
// kmalloc.c - generic memory allocator
//

#include <kmalloc.h>
#include <mm/types.h>
#include <utils.h>

// For allocations <= PAGE_SIZE
#include <mm/slab.h>

// For allocations > PAGE_SIZE
#include <mm/vma.h>

// FIXME: Maybe look into alignment at some point
void *kmalloc(size_t bytes) {
	if (bytes <= PAGE_SIZE)
		return slab_alloc(bytes);
	return vmalloc(bytes);
}

void *kzalloc(size_t bytes) {
	void *buf = kmalloc(bytes);
	memset(buf, 0, bytes);
	return buf;
}

void kfree(void *ptr) {
	if (!ptr)
		return;
	phys_addr addr = (phys_addr)ptr;
	if (addr >= PFA_VIRT_OFFSET)
		slab_free(ptr);
	else {
		vmfree(ptr);
	}
}
