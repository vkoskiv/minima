#include "pfa.h"
#include "terminal.h"
#include "panic.h"
#include "assert.h"

// struct page_frame {
	
// };

// // Loop through pages and write a bit of data to start of each physical page
// // that contains linkage
// void initialize_region(struct phys_region *r) {
	
// }

#define CONVENTIONAL_BYTES (640 * KB)
#define CONVENTIONAL_PAGES (CONVENTIONAL_BYTES / PAGE_SIZE)
#define MEG_PAGES ((1024*1024)/PAGE_SIZE)
struct phys_region phys_regions[3];
// The bootloader queries BIOS int 15h ah = 88h for us
// which tells us the number of contiguous kilobytes starting at
// 1MB (0x100000 phys).
void init_phys_mem_map(uint16_t mem_kb) {
	// FIXME: For now, I'm patching in <1MB by hand and we'll just mark it as occupied
	// until I know what to do with it.
	// See linux arch/x86/kernel/e820.c, they patch in LOWMEMSIZE() and then later mark reserved bits.

	// Conventional memory. This is also where mbr.S loads our kernel image, starting at
	// 0x10000
	phys_regions[0] = (struct phys_region){
		.start = 0x0, .pages = CONVENTIONAL_PAGES, // 0x00000-0x0ffff
		.next_free_frame = CONVENTIONAL_PAGES, // TODO: Bitmap
	};
	phys_regions[1] = (struct phys_region){
		.start = CONVENTIONAL_PAGES, .pages = (MEG_PAGES - CONVENTIONAL_PAGES), // 0xA0000-0x100000
		.next_free_frame = (MEG_PAGES - CONVENTIONAL_PAGES), // TODO: Bitmap
	};
	phys_regions[2] = (struct phys_region){
		.start = MEG_PAGES, .pages = mem_kb >> 2,
		.next_free_frame = MEG_PAGES,
	};
}

void dump_phys_mem_stats(void) {
	size_t total_pages = 0;
	kprintf("phys_regions:\n");
	for (size_t i = 0; i < 3; ++i) {
		struct phys_region *r = &phys_regions[i];
		uint32_t free_pages = r->pages - r->next_free_frame;
		total_pages += free_pages;
		uint32_t region_kb = (r->pages * PAGE_SIZE) / 1024;
		kprintf("\t[%i]: %h-%h (%ikB, %i free pages, next: %i)\n", i, from_pfn(r->start), from_pfn(r->start + r->pages), region_kb, free_pages, r->next_free_frame);
	}
	kprintf("%i free pages\n", total_pages);
}

phys_addr pf_allocate(void) {
	size_t ri = 0;
	for (ri = 0; ri < (sizeof(phys_regions) / sizeof(phys_regions[0])); ++ri) {
		struct phys_region *r = &phys_regions[ri];
		if (!(r->pages - r->next_free_frame))
			continue;
		phys_addr page = from_pfn(r->next_free_frame++);
		ASSERT((page & 0xfff) == 0);
		return page;
	}
	kprintf("No more free pages in region %i\n", ri);
	panic();
	return 0;
}
