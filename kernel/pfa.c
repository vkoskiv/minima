#include "pfa.h"
#include "terminal.h"
#include "panic.h"
#include "assert.h"
#include "assert.h"

struct page_frame {
	struct page_frame *next;
};

static struct page_frame *page_freelist = NULL;

#define CONVENTIONAL_BYTES (640 * KB)
#define CONVENTIONAL_PAGES (CONVENTIONAL_BYTES / PAGE_SIZE)
#define MEG_PAGES (MB / PAGE_SIZE)
struct phys_region phys_regions[3];

// Above 1MB
static uint32_t s_total_kb = 0;

extern uint32_t stage0_page_directory;

// stage0.c
extern pfn_t stage0_freelist_map_end;

// FIXME: hack
static uint8_t s_scratchbuf[256];

void map_freelist(void) {
	v_ma a = v_ma_from_buf(s_scratchbuf, 256);
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
	// FIXME: stage0 maps first page table statically (stage0_page_table2), and it reports the highest
	// pfn it mapped to 0xD0000000 in stage0_freelist_map_end. We ignore the first 1MB for page
	// frame allocation for now, so subtract 1024 - 256 = 768 pages to get the remaining
	// amount of page frames we need to map.
	const uint32_t pages_needed = (s_total_kb / 4) - (stage0_freelist_map_end - MEG_PAGES);
	ASSERT(((s_total_kb * 1024) / PAGE_SIZE) == (pages_needed + (stage0_freelist_map_end - MEG_PAGES)));

	const uint32_t page_tables_needed = (pages_needed / 1024) + 1;
	const size_t pd_start_idx = (PFA_VIRT_OFFSET / (4 * MB)) + 1;

	pfn_t cur_pfn = stage0_freelist_map_end;
	struct page_table **tables = v_new(&a, struct page_table *, page_tables_needed);
	for (size_t i = 0; i < page_tables_needed; ++i) {
		tables[i] = (struct page_table *)pf_allocate();
		for (size_t j = 0; j < 1024; ++j) {
			phys_addr addr = PFN_TO_PHYS(cur_pfn++);// + PFA_VIRT_OFFSET;
			ASSERT(addr > 0);
			// TODO: Really weird. Bitfields seem to just be entirely broken? Even
			// after setting with that working uint32 bitmask expression below, the commented out
			// assert fails. Huh!
			// TODO: Actually just noticed I was missing attribute(packed), maybe try that next?
			// tables[i]->entries[j] = (pte_t){
			// 	.page_addr = addr,//PFN_TO_PHYS(cur_pfn++),
			// 	.writable = 1,
			// 	.present = 1,
			// };
			// ASSERT(tables[i]->entries[j].page_addr == addr);
			*((uint32_t *)&tables[i]->entries[j]) = ((uint32_t)(addr | PTE_WRITABLE | PTE_PRESENT));
			ASSERT((tables[i]->entries[j] & 0xFFFFF000) == addr);
		}
	}
	uint32_t *pd_ptr = &stage0_page_directory;
	// Tables prepared, now I'll insert them in our page directory
	for (size_t i = 0; i < page_tables_needed; ++i) {
		phys_addr phys = ((uint32_t)tables[i]) - PFA_VIRT_OFFSET;
		pd_ptr[pd_start_idx + i] = phys | PTE_WRITABLE | PTE_PRESENT;
	}
	flush_cr3();
	int asdf = 0;
	(void)asdf;
}

void map_phys_region(struct phys_region r) {
	if (r.start < MEG_PAGES) // FIXME
		return;
	for (pfn_t p = r.start; p < (r.start + r.pages); ++p) {
		void *page = (void *)(PFN_TO_PHYS(p) + PFA_VIRT_OFFSET);
		// kprintf("page: %h, phys: %h\n", page, get_physical_address((virt_addr)page));
		if (p < 1024) // FIXME: Map to loop at end of init_phys_mem_map
			continue;
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
	map_freelist();
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
		kprintf("\t[%i]: %h-%h (%ikB, %i/%i free)\n", i, from_pfn(r->start), from_pfn(r->start + r->pages), region_kb, free_pages_per_region[i], r->pages);
	}
	kprintf("%i pages (%ikB) free\n", total_free_pages, (total_free_pages * PAGE_SIZE) / 1024);
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
