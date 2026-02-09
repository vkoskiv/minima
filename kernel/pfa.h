/*
	Page frame allocator. This bit keeps track of physical pages.
*/

#include "mman.h"

typedef uint32_t pfn_t;

struct phys_region {
	pfn_t start;
	uint32_t pages;
	pfn_t next_free_frame;
};

void init_phys_mem_map(uint16_t mem_kb);
void dump_phys_mem_stats(void);

phys_addr pf_allocate(void);

static inline phys_addr from_pfn(pfn_t p) {
	return p << 12;
}
