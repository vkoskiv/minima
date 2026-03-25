/*
	Page frame allocator. This bit keeps track of physical pages.
*/

#ifndef _PFA_H_
#define _PFA_H_

#include <vkern.h>
#include <mm/types.h>

#define CONVENTIONAL_BYTES (640 * KB)
#define CONVENTIONAL_PAGES (CONVENTIONAL_BYTES / PAGE_SIZE)
#define MEG_PAGES (MB / PAGE_SIZE)

#define PHYS_REGION_IGNORE			(0x1 << 0)

typedef uint32_t pfn_t;

#define PFN_TO_PHYS(pfn) ((phys_addr)((pfn) * PAGE_SIZE))
#define PFN_FROM_PHYS(phys) ((phys) / PAGE_SIZE)

// stage0 calls this to add new region for >1MB
int pfa_register_region(const char *name, pfn_t start, uint32_t pages, uint32_t flags);
int pfa_register_reserved_region(const char *name, pfn_t start, uint32_t pages);
// stage1 calls this after setting up fault handlers to populate freelists.
// After this call, pf_alloc()/pf_free() can be used.
void pfa_init(void);
void dump_phys_mem_stats(v_ma a, int show_regions);

/*
	These allocate and free individual blocks of PAGE_SIZE bytes
	mapped at PFA_VIRT_OFFSET.
*/
void *pf_alloc(void);
void *pf_zalloc(void);
void pf_free(void *page);

uint32_t pfa_count_free_pages(void);

static inline phys_addr from_pfn(pfn_t p) {
	return p << 12;
}

#endif
