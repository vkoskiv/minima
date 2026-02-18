#include "pfa.h"

void set_up_stage0_page_tables(void);

/*
	Note: For this function to align at exactly KERNEL_PHYS_ADDR in the final kernel image,
	it's actually important that this function is the first one in this file. Otherwise
	the bootloader blind jump to KERNEL_PHYS_ADDR will start executing stage0 in the wrong function.

	Stage0 runs in low memory in 32-bit protected mode. It sets up temporary page tables to
	map high memory, and jumps to stage1_init in high memory.
*/
extern void _stage0_init(uint16_t mem_kb, uint16_t pad0, uint32_t pad1, uint32_t pad2) {
	(void)pad0; (void)pad1; (void)pad2;
	set_up_stage0_page_tables();
	// Now we can access stuff in .text, which is mapped at +0xC0000000
	init_phys_mem_map(mem_kb); // This call also populates pfa with available <1MB pages
	

	// Jump to higher half now
	asm volatile(
		"addl %0, %%esp\n\t"
		"jmp stage1_init"
		: /* No outputs */
		: "i"(VIRT_OFFSET)
		: /* No clobbers */);
	asm volatile("cli; hlt");
}

uint32_t stage0_page_directory[1024] __attribute__((aligned(PAGE_SIZE)));
uint32_t stage0_page_table1[1024] __attribute((aligned(PAGE_SIZE)));

// This maps first 4MB of memory to 0xD0000000 to bootstrap page frame allocator
// free-list setup.
uint32_t stage0_page_table2[1024] __attribute((aligned(PAGE_SIZE)));

// stage0.S
extern void load_page_directory(phys_addr);
extern void enable_paging(void);

pfn_t stage0_last_mapped_pfn = 0;

void set_up_stage0_page_tables(void) {
	for (int i = 0; i < 1024; ++i)
		stage0_page_directory[i] = PTE_WRITABLE; // r/w, only accessible by kernel, not present
	// Set up initial mappings:
	//         0x000000-kernel_phys_end -> 0xC0000000-kernel_phys_end+VIRT_OFFSET
	//         (kernel image, resides @ 0x10000 conventional)
	//     4MB 0x000000-0x3fffff -> 0xD0000000-0xD03fffff (for page frame allocator bootstrap)
	// TODO: Really what I should be doing here is memcpying the kernel image
	// to where mem_map starts accounting, then mark those pages used up accordingly,
	// then continue with the page frame allocator from there.

	pfn_t last_page = PFN_FROM_PHYS(PAGE_ROUND_UP(kernel_physical_end));
	pfn_t p;
	for (p = 0; p < last_page; ++p)
		stage0_page_table1[p] = ((p * PAGE_SIZE)) | PTE_WRITABLE | PTE_PRESENT;

	// FIXME: This places the VGA buffer to 0xC00B8000 where terminal.c expects to find it.
	// Find a dedicate space for this at some point. For now the virtual space allocator
	// just starts at VIRT_OFFSET + 1MB to avoid clobbering the kernel & this.
	stage0_page_table1[184] = 0x000B8000 | 0x3;

	for (p = 0; p < ((4 * MB) / PAGE_SIZE); ++p)
		stage0_page_table2[p] = ((p * PAGE_SIZE)) | PTE_WRITABLE | PTE_PRESENT;

	stage0_last_mapped_pfn = p;

	// Map page 0 as not present to catch a NP page fault on NULL deref/write
	stage0_page_table1[0] = 0x00000000;

	// 0xC0000000 main mapping, code is linked here.
	stage0_page_directory[(VIRT_OFFSET / (4 * MB))] = (uint32_t)&stage0_page_table1[0] | PTE_WRITABLE | PTE_PRESENT;

	// 0xD0000000 mapping for page frame allocator to poke at physical pages.
	// This mapping is extended >4MB, if needed, in stage1 pfa_init()
	stage0_page_directory[(PFA_VIRT_OFFSET / (4 * MB))] = (uint32_t)&stage0_page_table2[0] | PTE_WRITABLE | PTE_PRESENT;

	// Also identity map, for now.
	stage0_page_directory[0] = (uint32_t)&stage0_page_table1[0] | 3;
	// And map page directory, so we can still poke at it. Doing this makes the PD
	// accessible at 0xFFFFF000, since the CPU grabs the last PDE, interprets it
	// as a page table, then grabs the last entry, which is also the last entry
	// in the PD, which points to the PD base itself.
	stage0_page_directory[1023] = (uint32_t)&stage0_page_directory[0] | 3;
	load_page_directory((phys_addr)&stage0_page_directory[0]);
	enable_paging();
}
