#include "pfa.h"
#include "terminal.h"
#include "panic.h"
#include "assert.h"
#include "linker.h"

struct page_frame {
	struct page_frame *next;
};

static struct page_frame *page_freelist = NULL;

#define CONVENTIONAL_BYTES (640 * KB)
#define CONVENTIONAL_PAGES (CONVENTIONAL_BYTES / PAGE_SIZE)
#define MEG_PAGES (MB / PAGE_SIZE)

#define PHYS_REGION_IGNORE			(0x1 << 0)

struct phys_region {
	pfn_t start;
	uint32_t pages;
	uint32_t reserved;
	uint32_t flags;
};

struct phys_region phys_regions[3];

// Above 1MB
static uint32_t s_total_kb = 0;

extern uint32_t stage0_page_directory;

// FIXME: hack
static uint8_t s_scratchbuf[256];

extern pfn_t stage0_last_mapped_pfn;

void map_above_4_meg_freelist(void) {
	if ((s_total_kb + 1024) <= (4 * KB)) {
		kprintf("pfa: total mem %ik <= %ik, skipping stage1 freelist map\n", s_total_kb + 1024, (4 * KB));
		return; // <= 4MB memory in this system, we don't need any more page tables.
	}
	v_ma a = v_ma_from_buf(s_scratchbuf, 256);
	/*
		Idea here is to map all physical pages >4MB to virt 0xD0400000->
		so we can poke at our freelist there.
		Stage0 already populated <640k pages around the kernel image for us,
		so we can now use pf_alloc() to allocate page tables to map rest of memory.
		Let's make sure we have some pages, first of all:
	*/
	ASSERT(page_freelist != NULL);
	size_t freelist_pages = 0;
	struct page_frame *f = page_freelist;
	while ((f = f->next))
		freelist_pages++;
	/*
		First figure out how many pages >4MB mem we need to map
		Then divide that by 1024 to get amount of page tables needed
		pf_alloc that many page tables, probably into an array?
		Populate those
		put them in stage0_page_directory
		flush tlb/cr3
		then return, so map_phys_regions can populatee remaining freelists.
	*/
	const uint32_t pages_above_1meg = (s_total_kb / 4);
	const uint32_t pages_needed = pages_above_1meg - ((3 * MB) / PAGE_SIZE);
	const uint32_t page_tables_needed = (pages_needed / 1024) + 1; // TODO: +1 needed?
	ASSERT(freelist_pages >= page_tables_needed);

	kprintf("pfa: Allocating %i new page tables to map remaining %i pages (%ik)\n",
	        page_tables_needed, pages_needed, (pages_needed * PAGE_SIZE) / 1024);
	const size_t pd_start_idx = (PFA_VIRT_OFFSET / (4 * MB)) + 1;

	pfn_t cur_pfn = stage0_last_mapped_pfn;
	// Hard-coded for now, but could only map needed amount later.
	ASSERT(stage0_last_mapped_pfn == 1024);
	struct page_table **tables = v_new(&a, struct page_table *, page_tables_needed);
	for (size_t i = 0; i < page_tables_needed; ++i) {
		tables[i] = (struct page_table *)pf_alloc();
		for (size_t j = 0; j < 1024; ++j) {
			phys_addr addr = PFN_TO_PHYS(cur_pfn++);// + PFA_VIRT_OFFSET;
			ASSERT(addr > 0);
			*((uint32_t *)&tables[i]->entries[j]) = ((uint32_t)(addr | PTE_WRITABLE | PTE_PRESENT));
		}
	}
	uint32_t *pd_ptr = &stage0_page_directory;
	// Tables prepared, now insert them in our page directory
	for (size_t i = 0; i < page_tables_needed; ++i) {
		phys_addr phys = ((uint32_t)tables[i]) - PFA_VIRT_OFFSET;
		pd_ptr[pd_start_idx + i] = phys | PTE_WRITABLE | PTE_PRESENT;
	}
	flush_cr3();
}

static void map_phys_region(struct phys_region *r) {
	const pfn_t kernel_image_start_pfn = PFN_FROM_PHYS(kernel_physical_start);
	const pfn_t kernel_image_end_pfn = PFN_FROM_PHYS(PAGE_ROUND_UP(kernel_physical_end));
	const pfn_t stack_bottom_pfn = PFN_FROM_PHYS(STACK_BOTTOM);
	const pfn_t stack_top_pfn = PFN_FROM_PHYS(STACK_TOP);
	for (pfn_t p = r->start; p < (r->start + r->pages); ++p) {
		// TODO: Consider a more generic table of e.g. `struct reserved_region`
		if ((kernel_image_start_pfn <= p && p <= kernel_image_end_pfn) ||
				  (stack_bottom_pfn <= p && p <= stack_top_pfn)) {
			r->reserved++;
			continue;
		}
		void *page = (void *)(PFN_TO_PHYS(p) + PFA_VIRT_OFFSET);
		// kprintf("page: %h, phys: %h\n", page, get_physical_address((virt_addr)page));
		ASSERT(((phys_addr)page & 0xfff) == 0);
		pf_free(page);
	}
}

static void map_phys_regions(void) {
	for (size_t i = 0; i < (sizeof(phys_regions)/sizeof(phys_regions[0])); ++i) {
		if (!(phys_regions[i].flags & PHYS_REGION_IGNORE))
			map_phys_region(&phys_regions[i]);
	}
}

// The bootloader queries BIOS int 15h ah = 88h for us
// which tells us the number of contiguous kilobytes starting at
// 1MB (0x100000 phys).
void init_phys_mem_map(uint16_t mem_kb) {
	s_total_kb = mem_kb;
	// See linux arch/x86/kernel/e820.c, they patch in LOWMEMSIZE() and then later mark reserved bits.

	// Conventional memory. This is also where mbr.S loads our kernel image,
	// starting at KERNEL_PHYS_ADDR
	phys_regions[0] = (struct phys_region){
		.start = 0x0, .pages = CONVENTIONAL_PAGES, // 0x00000000-0x0009ffff
		.flags = PHYS_REGION_IGNORE, // Stage0 handles this
	};
	phys_regions[1] = (struct phys_region){
		.start = CONVENTIONAL_PAGES, .pages = (MEG_PAGES - CONVENTIONAL_PAGES), // 0x000A0000-0x000fffff
		.flags = PHYS_REGION_IGNORE, // For now, at least. Bunch of memory-mapped hw here, including our VGA buf.
	};
	phys_regions[2] = (struct phys_region){
		.start = MEG_PAGES, .pages = mem_kb >> 2,
	};

	// Add available conventional memory to the freelist now, so pfa_init can use that memory
	// to allocate more page tables.
	map_phys_region(&phys_regions[0]);
}

void pfa_init(void) {
	map_above_4_meg_freelist();
	map_phys_regions();
}

void dump_phys_mem_stats(v_ma a) {
	size_t n_regions =  (sizeof(phys_regions) / sizeof(phys_regions[0]));
	uint32_t *free_pages_per_region = v_new(&a, uint32_t, n_regions);
	for (size_t i = 0; i < n_regions; ++i)
		ASSERT(free_pages_per_region[i] == 0);
	kprintf("phys_regions:\n");
	// FIXME: This is really inefficient, but will work for now.
	struct page_frame *frame = page_freelist;
	while (frame) {
		pfn_t pfn = PFN_FROM_PHYS((uint32_t)frame - PFA_VIRT_OFFSET);
		frame = frame->next;
		for (size_t i = 0; i < n_regions; ++i) {
			struct phys_region *r = &phys_regions[i];
			if (pfn >= r->start && pfn < (r->start + r->pages))
				free_pages_per_region[i]++;
		}
	}
	uint32_t total_free_pages = 0;
	for (size_t i = 0; i < n_regions; ++i) {
		struct phys_region *r = &phys_regions[i];
		total_free_pages += free_pages_per_region[i];
		uint32_t region_kb = (r->pages * PAGE_SIZE) / 1024;
		kprintf("\t[%i]: %h-%h (%ikB, %i/%i free, %i reserved)\n", i, from_pfn(r->start), from_pfn(r->start + r->pages) - 1, region_kb, free_pages_per_region[i], r->pages, r->reserved);
	}
	kprintf("%i pages (%ikB) free\n", total_free_pages, (total_free_pages * PAGE_SIZE) / 1024);
}

void *pf_alloc(void) {
	if (!page_freelist)
		return NULL;
	void *page = page_freelist;
	page_freelist = page_freelist->next;
	// FIXME: Probably gate this behind some debug flag at some point.
	// I'll leave it in for now to catch bugs, performance will come later.
	memset(page, 0x41, PAGE_SIZE);
	return page;
}

void pf_free(void *page) {
	struct page_frame *frame = (struct page_frame *)page;
	frame->next = page_freelist;
	page_freelist = frame;
}
