//
// kmalloc.c - generic memory allocator
//

#include <kmalloc.h>
#include <mm/types.h>

// For allocations <= PAGE_SIZE
#include <mm/pfa.h>

// For allocations > PAGE_SIZE
#include <mm/vma.h>

void *kmalloc(size_t bytes) {
	if (bytes <= PAGE_SIZE)
		return pf_alloc();
	return vmalloc(bytes);
}

void kfree(void *ptr) {
	if (!ptr)
		return;
	phys_addr addr = (phys_addr)ptr;
	if (addr >= PFA_VIRT_OFFSET)
		pf_free(ptr);
	else {
		vmfree(ptr);
	}
}
