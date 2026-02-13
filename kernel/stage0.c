#include "pfa.h"

extern void stage1_init();
void set_up_stage0_page_tables(void);

/*
	Note: For this function to align at exactly 0x10000 in the final kernel image,
	it's actually important that this function is the first one in this file. Otherwise
	the bootloader blind jump to 0x10000 will start executing stage0 in the wrong function.

	Stage0 runs in low memory in 32-bit protected mode. It sets up temporary page tables to
	map high memory, and jumps to stage1_init in high memory.
*/
extern void _stage0_init(uint16_t mem_kb, uint16_t pad0, uint32_t pad1, uint32_t pad2) {
	(void)pad0; (void)pad1; (void)pad2;
	set_up_stage0_page_tables();
	// Now we can access stuff in .text, which is mapped at +0xC0000000
	init_phys_mem_map(mem_kb);
	

	// Jump to higher half now
	asm volatile("addl $0xC0000000, %%esp" : : : );
	stage1_init();
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

pfn_t stage0_freelist_map_end = 0;

void set_up_stage0_page_tables(void) {
	for (int i = 0; i < 1024; ++i)
		stage0_page_directory[i] = PTE_WRITABLE; // r/w, only accessible by kernel, not present
	// Set up initial mapping, map 4MB starting from 0x00000000, so
	// 0x00000000-0x003fffff -> 0xC0000000-0xC03fffff
	// TODO: Really what I should be doing here is memcpying the kernel image
	// to where mem_map starts accounting, then mark those pages used up accordingly,
	// then continue with the page frame allocator from there.
	for (int i = 0; i < 1024; ++i)
		stage0_page_table1[i] = ((i * PAGE_SIZE)) | PTE_WRITABLE | PTE_PRESENT;

	// Map page 0 as not present to catch a NP page fault on NULL deref/write
	stage0_page_table1[0] = 0x00000000;

	// VGA buf
	// Seems I can comment this out and things still work. Learn why.
	//     ->Learned it now. The above 4MB mapping covers this address already.
	// page_table1[1023] = 0x000B8000 | 0x3;

	// 768 => 0xC0000000->
	stage0_page_directory[768] = (uint32_t)&stage0_page_table1[0] | PTE_WRITABLE | PTE_PRESENT;

	// Also map 4MB from 0x0-> to our physical page map region at 0xD0000000
	for (stage0_freelist_map_end = 0; stage0_freelist_map_end < 1024; ++stage0_freelist_map_end)
		stage0_page_table2[stage0_freelist_map_end] = (stage0_freelist_map_end * PAGE_SIZE) | PTE_WRITABLE | PTE_PRESENT;

	// FIXME: Probably find a smarter way eventually
	// 0x000000-3ff000 -> 0xD0000000-0xD03ff000
	stage0_page_directory[(PFA_VIRT_OFFSET / (4 * MB))] = (uint32_t)&stage0_page_table2[0] | PTE_WRITABLE | PTE_PRESENT;

	// Also identity map, for now.
	stage0_page_directory[0] = (uint32_t)&stage0_page_table1[0] | 3;
	// And map page directory, so we can still poke at it.
	stage0_page_directory[1023] = (uint32_t)&stage0_page_directory[0] | 3;
	load_page_directory((phys_addr)&stage0_page_directory[0]);
	enable_paging();
}
