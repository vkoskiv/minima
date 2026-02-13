/*
	Page frame allocator. This bit keeps track of physical pages.
*/

#include "mman.h"
#include <vkern.h>

typedef uint32_t pfn_t;

#define PFN_TO_PHYS(pfn) ((phys_addr)((pfn) * PAGE_SIZE))
#define PFN_FROM_PHYS(phys) ((phys) / PAGE_SIZE)

// stage0 calls this to populate the memory map
void init_phys_mem_map(uint16_t mem_kb);
// stage1 calls this after setting up fault handlers to populate freelists.
// After this call, pf_allocate()/pf_free() can be used.
void pfa_init(void);
void dump_phys_mem_stats(v_ma a);

void *pf_allocate(void);
void pf_free(void *page);

static inline phys_addr from_pfn(pfn_t p) {
	return p << 12;
}
