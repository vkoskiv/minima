#include "pfa.h"
#include "terminal.h"
#include "panic.h"
#include "assert.h"
#include "assert.h"

struct page_frame {
	struct page_frame *next;
};

static struct page_frame *page_freelist = NULL;

// // Loop through pages and write a bit of data to start of each physical page
// // that contains linkage
// void initialize_region(struct phys_region *r) {
	
// }

#define CONVENTIONAL_BYTES (640 * KB)
#define CONVENTIONAL_PAGES (CONVENTIONAL_BYTES / PAGE_SIZE)
#define MEG_PAGES (MB / PAGE_SIZE)
struct phys_region phys_regions[3];

// Above 1MB
static uint32_t s_total_kb = 0;

extern uint32_t stage0_page_directory;

void map_physical_mem(void) {
	/*
		Idea here is to map all physical pages >1MB to virt 0xD0000000->
		so we can poke at our freelist there.

		First figure out how many pages of mem we have to map
		Then subtract 1024 (stage0.c stage0_page_table2 maps first 1024 pages)
		Divide that by 1024 to get amount of page tables needed
		pf_allocate that many page tables, probably into an array?
		Populate those
		put them in stage0_page_directory
		flush tlb/cr3
		then return, so map_phys_regions can populate the freelists.
	*/
	// const uint32_t pages_needed = (s_total_kb / 4) - 1024;
	// const uint32_t page_tables_needed = (pages_needed / 1024) + 1;
	// // FIXME: stage0_page_table2 maps the first page table, hence + 1 here and - 1024 above.
	// const size_t pd_start_idx = (PFA_VIRT_OFFSET / (4 * MB)) + 1;

	// for (size_t i = 0; i < page_tables_needed; ++i) {

	// }
}

void map_phys_region(struct phys_region r) {
	if (r.start < MEG_PAGES) // FIXME
		return;
	for (pfn_t p = r.start; p < (r.start + r.pages); ++p) {
		// if (p < stage0_highest_mapped_pfn) // FIXME
		// 	continue;
		void *page = (void *)PFN_TO_PHYS(p);
		ASSERT(((phys_addr)page & 0xfff) == 0);
		pf_free(page);
	}
}

void map_phys_regions(void) {
	for (size_t i = 0; i < (sizeof(phys_regions)/sizeof(phys_regions[0])); ++i)
		map_phys_region(phys_regions[i]);
}

// The bootloader queries BIOS int 15h ah = 88h for us
// which tells us the number of contiguous kilobytes starting at
// 1MB (0x100000 phys).
void init_phys_mem_map(uint16_t mem_kb) {
	s_total_kb = mem_kb;
	// FIXME: For now, I'm patching in <1MB by hand and we'll just mark it as occupied
	// until I know what to do with it.
	// See linux arch/x86/kernel/e820.c, they patch in LOWMEMSIZE() and then later mark reserved bits.

	// Conventional memory. This is also where mbr.S loads our kernel image, starting at
	// 0x10000
	phys_regions[0] = (struct phys_region){
		.start = 0x0, .pages = CONVENTIONAL_PAGES, // 0x00000-0x0ffff
	};
	phys_regions[1] = (struct phys_region){
		.start = CONVENTIONAL_PAGES, .pages = (MEG_PAGES - CONVENTIONAL_PAGES), // 0xA0000-0x100000
	};
	phys_regions[2] = (struct phys_region){
		.start = MEG_PAGES, .pages = mem_kb >> 2,
	};

	// Add stage0 mapped pages to freelist. stage0 maps 4MB starting at 0x0, and our pfa will allocate
	// starting from 0x100000 onwards, so add 3MB to the freelist now.
	for (size_t i = 256; i < 1024; ++i)
		pf_free((void *)(PFN_TO_PHYS(i) + PFA_VIRT_OFFSET));
}

void pfa_init(void) {
	// map_physical_mem();
	// map_phys_regions();
}

void dump_phys_mem_stats(v_ma a) {
	size_t n_regions =  (sizeof(phys_regions) / sizeof(phys_regions[0]));
	uint32_t *free_pages_per_region = v_new(&a, uint32_t, n_regions);
	for (size_t i = 0; i < n_regions; ++i)
		ASSERT(free_pages_per_region[i] == 0);
	kprintf("phys_regions:\n");
	// FIXME: This is really inefficient, but will work for now.
	struct page_frame *frame = page_freelist;
	// size_t asdf = 0;
	while (frame) {
		pfn_t pfn = PFN_FROM_PHYS((uint32_t)frame - PFA_VIRT_OFFSET);
		frame = frame->next;
		// kprintf("%i, frame: %h, frame->next: %h\n", asdf++, frame, frame->next);
		for (size_t i = 0; i < n_regions; ++i) {
			struct phys_region *r = &phys_regions[i];
			if (pfn > r->start && pfn < (r->start + r->pages))
				free_pages_per_region[i]++;
		}
	}
	uint32_t total_free_pages = 0;
	for (size_t i = 0; i < n_regions; ++i) {
		struct phys_region *r = &phys_regions[i];
		total_free_pages += free_pages_per_region[i];
		uint32_t region_kb = (r->pages * PAGE_SIZE) / 1024;
		kprintf("\t[%i]: %h-%h (%ikB, %i free pages)\n", i, from_pfn(r->start), from_pfn(r->start + r->pages), region_kb, free_pages_per_region[i]);
	}
	kprintf("%i free pages\n", total_free_pages);
}

void *pf_allocate(void) {
	if (!page_freelist) {
		kprintf("!page_freelist\n");
		panic();
	}
	void *page = page_freelist;
	page_freelist = page_freelist->next;
	return page;
}

void pf_free(void *page) {
	struct page_frame *frame = (struct page_frame *)page;
	frame->next = page_freelist;
	page_freelist = frame;
}
