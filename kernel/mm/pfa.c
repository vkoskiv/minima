#include <mm/pfa.h>
#include <mm/vma.h>
#include <kprintf.h>
#include <assert.h>
#include <linker.h>
#include <x86.h>
#include <mm/purge.h>

struct page_frame {
	struct page_frame *next;
};

static struct page_frame *volatile page_freelist = NULL;

struct region {
	const char *name;
	pfn_t start;
	uint32_t pages;
};
struct phys_region {
	struct region r;
	uint32_t reserved;
	uint32_t flags;
};

#define MAX_PHYS_REGIONS 8
struct phys_region phys_regions[8] = { 0 };
static size_t s_next_region = 0;

#define MAX_RESVD_REGIONS 8
static struct region s_reserved[MAX_RESVD_REGIONS] = {
	{ .name = "null_page",       .start = 0, .pages = 1 },
	{ .name = "stage0_pd",       .start = PFN_FROM_PHYS(STAGE0_PD_ADDR),  .pages = 1 },
	{ .name = "stage0_pt0",      .start = PFN_FROM_PHYS(STAGE0_PT1_ADDR), .pages = 1 },
	{ .name = "stage0_pt1",       .start = PFN_FROM_PHYS(STAGE0_PT2_ADDR), .pages = 1 },
	{ .name = "bootstrap_stack", .start = STACK_BOTTOM >> 12, .pages = ((STACK_TOP >> 12) - (STACK_BOTTOM >> 12)) },
	{ .name = "dma_buf",         .start = DMA_BUF_ADDR >> 12, .pages = DMA_BUF_SIZE / PAGE_SIZE },
	// kernel_image will be marked here in pfa_init()
};

static size_t s_next_reserved = 6;

extern pfn_t stage0_last_mapped_pfn;

// FIXME: Delete this horrible function
static void map_above_4_meg_freelist(void) {

	uint32_t total_kb_above_1meg = 0;
	for (size_t i = 0; i < s_next_region; ++i) {
		if (phys_regions[i].r.start >= MEG_PAGES)
			total_kb_above_1meg += phys_regions[i].r.pages << 2;
	}
	
	if ((total_kb_above_1meg + 1024) <= (4 * KB)) {
		kprintf("pfa: total mem %ik <= %ik, skipping stage1 freelist map\n", total_kb_above_1meg + 1024, (4 * KB));
		return; // <= 4MB memory in this system, we don't need any more page tables.
	}
	/*
		Idea here is to map all physical pages >4MB to virt 0xD0400000->
		so we can poke at our freelist there.
		Stage0 already populated <640k pages around the kernel image for us,
		so we can now use pf_alloc() to allocate page tables to map rest of memory.
		Let's make sure we have some pages, first of all:
	*/
	assert(page_freelist != NULL);
	uint8_t *scratch = pf_alloc();
	v_ma a = v_ma_from_buf(scratch, PAGE_SIZE);
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
	const uint32_t pages_above_1meg = (total_kb_above_1meg / 4);
	const uint32_t pages_needed = pages_above_1meg - ((3 * MB) / PAGE_SIZE);
	const uint32_t page_tables_needed = (pages_needed / 1024) + 1; // TODO: +1 needed?
	assert(freelist_pages >= page_tables_needed);

	const size_t pd_start_idx = (PFA_VIRT_OFFSET / (4 * MB)) + 1;

	pfn_t cur_pfn = stage0_last_mapped_pfn;
	// Hard-coded for now, but could only map needed amount later.
	assert(stage0_last_mapped_pfn == 1024);
	struct page_table **tables = v_new(&a, struct page_table *, page_tables_needed);
	for (size_t i = 0; i < page_tables_needed; ++i) {
		tables[i] = (struct page_table *)pf_alloc();
		for (size_t j = 0; j < 1024; ++j) {
			phys_addr addr = PFN_TO_PHYS(cur_pfn++);// + PFA_VIRT_OFFSET;
			assert(addr > 0);
			*((uint32_t *)&tables[i]->entries[j]) = ((uint32_t)(addr | PTE_WRITABLE | PTE_PRESENT));
		}
	}
	uint32_t *pd_ptr = (uint32_t *)0xFFFFF000;
	// Tables prepared, now insert them in our page directory
	for (size_t i = 0; i < page_tables_needed; ++i) {
		phys_addr phys = ((uint32_t)tables[i]) - PFA_VIRT_OFFSET;
		pd_ptr[pd_start_idx + i] = phys | PTE_WRITABLE | PTE_PRESENT;
	}
	flush_cr3();
	pf_free(scratch);
}

static int is_reserved(pfn_t p) {
	for (size_t i = 0; i < s_next_reserved; ++i) {
		struct region *r = &s_reserved[i];
		if (r->start <= p && p <= (r->start + r->pages))
			return 1;
	}
	return 0;
}

static int region_overlaps(struct region *r, pfn_t start, uint32_t pages) {
	pfn_t reg_start = r->start;
	pfn_t reg_end = r->start + r->pages;
	if (start >= reg_start && start < reg_end)
		return 1;
	pfn_t end = start + pages;
	if (end >= reg_start && end < reg_end)
		return 1;
	return 0;
}

int pfa_register_reserved_region(const char *name, pfn_t start, uint32_t pages) {
	if (s_next_reserved >= MAX_RESVD_REGIONS)
		return 1;
	for (size_t i = 0; i < s_next_reserved; ++i) {
		if (region_overlaps(&s_reserved[i], start, pages))
			panic("Trying to register region '%s' that overlaps with region '%s'", name, s_reserved[i].name);
	}
	s_reserved[s_next_reserved++] = (struct region){
		.name = name,
		.start = start,
		.pages = pages,
	};
	return 0;
}

static void map_phys_region(struct phys_region *r) {
	for (pfn_t p = r->r.start; p < (r->r.start + r->r.pages); ++p) {
		if (is_reserved(p)) {
			r->reserved++;
			continue;
		}
		void *page = (void *)(PFN_TO_PHYS(p) + PFA_VIRT_OFFSET);
		assert(((phys_addr)page & 0xfff) == 0);
		pf_free(page);
	}
}

static void map_phys_regions(void) {
	for (size_t i = 0; i < s_next_region; ++i) {
		if (!(phys_regions[i].flags & PHYS_REGION_IGNORE))
			map_phys_region(&phys_regions[i]);
	}
}

int pfa_register_region(const char *name, pfn_t start, uint32_t pages, uint32_t flags) {
	// This may be called before jumping to higher half, so fix up string address accordingly
	if ((uintptr_t)name < VIRT_OFFSET)
		name += VIRT_OFFSET;
	if (s_next_region >= MAX_PHYS_REGIONS)
		return 1;
	phys_regions[s_next_region++] = (struct phys_region){
		.r = { .name = name, .start = start, .pages = pages },
		.flags = flags
	};
	return 0;
}

void pfa_init(void) {

	// Mark the kernel image reserved
	pfn_t kernel_start_pfn = PFN_FROM_PHYS(PAGE_ROUND_DN(kernel_physical_start));
	size_t kernel_pages = PFN_FROM_PHYS(PAGE_ROUND_UP(kernel_physical_end)) - kernel_start_pfn;
	pfa_register_reserved_region("kernel_image", PFN_FROM_PHYS(kernel_physical_start), kernel_pages);

	// Add available conventional memory to the freelist now, so map_above_4meg_freelist
	// can use that memory to allocate more page tables if needed.
	map_phys_region(&phys_regions[0]);

	map_above_4_meg_freelist();

	// Now map rest of physical regions
	map_phys_regions();
	size_t total_pages = 0;
	for (size_t i = 0; i < s_next_region; ++i)
		total_pages += phys_regions[i].r.pages;
	kprintf("mm/pfa: tracking %u page frames (%uk)\n", total_pages, (total_pages * PAGE_SIZE) / KB);
}

static void dump_reserved_regions(void) {
	for (size_t i = 0; i < s_next_reserved; ++i) {
		struct region *r = &s_reserved[i];
		kprintf("[%u]: %h-%h (%s)\n", i, PFN_TO_PHYS(r->start), PFN_TO_PHYS(r->start + r->pages), r->name);
	}
}

uint32_t pfa_count_free_pages(void) {
	uint32_t pages = 0;
	cli_push();
	struct page_frame *frame = page_freelist;
	while (frame) {
		pages++;
		frame = frame->next;
	}
	cli_pop();
	return pages;
}

void dump_phys_mem_stats(v_ma a, int show_regions) {
	uint32_t *free_pages_per_region = v_new(&a, uint32_t, s_next_region - 1);
	for (size_t i = 0; i < s_next_region; ++i)
		free_pages_per_region[i] = 0;
	if (show_regions)
		kprintf("phys_regions:\n");
	// FIXME: This is really inefficient, but will work for now.
	cli_push();
	struct page_frame *frame = page_freelist;
	while (frame) {
		pfn_t pfn = PFN_FROM_PHYS((uint32_t)frame - PFA_VIRT_OFFSET);
		frame = frame->next;
		for (size_t i = 0; i < s_next_region; ++i) {
			struct phys_region *r = &phys_regions[i];
			if (pfn >= r->r.start && pfn < (r->r.start + r->r.pages))
				free_pages_per_region[i]++;
		}
	}
	cli_pop();
	uint32_t total_free_pages = 0;
	for (size_t i = 0; i < s_next_region; ++i) {
		struct phys_region *r = &phys_regions[i];
		total_free_pages += free_pages_per_region[i];
		if (show_regions) {
			uint32_t region_kb = (r->r.pages * PAGE_SIZE) / 1024;
			kprintf("\t[%i]: %h-%h (%ikB '%s', %i/%i free, %i rsvd.)\n", i, from_pfn(r->r.start), from_pfn(r->r.start + r->r.pages) - 1, region_kb, r->r.name, free_pages_per_region[i], r->r.pages, r->reserved);
		}
	}
	if (show_regions)
		dump_reserved_regions();
	kprintf("%i pages (%ikB) free\n", total_free_pages, (total_free_pages * PAGE_SIZE) / 1024);
}

void *pf_alloc(void) {
	if (!page_freelist) {
		cli_push();
		kprintf("pfa: out of page frames, attempting to free some...\n");
		size_t purged = purgeable_purge(PAGE_SIZE);
		if (purged < PAGE_SIZE)
			return NULL;
		kprintf("pfa: purged %u bytes\n", purged);
		assert(page_freelist);
		cli_pop();
	}
	for (;;) {
		struct page_frame *old = page_freelist;
		struct page_frame *new = old->next;
		if (cmpxchg((int *)&page_freelist, (uintptr_t)old, (uintptr_t)new) == (int)old)
			return old;
	}
}

void *pf_zalloc(void) {
	void *page = pf_alloc();
	if (!page)
		return NULL;
	memset(page, 0x00, PAGE_SIZE);
	return page;
}

// TODO: Find out why this seemingly equivalent variant doesn't work.
// disassembly for this is in atomic_pf_free_bad.txt, and the working one is in
// atomic_pf_free_good.txt
// frame->next = page_freelist;
// if (cmpxchg((int *)&page_freelist, (int)frame->next, (int)frame) == (int)frame->next)
// 	return;

void pf_free(void *page) {
	struct page_frame *frame = (struct page_frame *)page;
	for (;;) {
		frame->next = page_freelist;
		struct page_frame *old = frame->next;
		struct page_frame *new = frame;
		if (cmpxchg((int *)&page_freelist, (int)old, (int)new) == (int)old)
			return;
	}
}
