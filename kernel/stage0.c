extern void kernel_main();
#include "pfa.h"

void set_up_stage0_page_tables(void);

/*
	Note: For this function to align at exactly 0x10000 in the final kernel image,
	it's actually important that this function is the first one in this file. Otherwise
	the bootloader blind jump to 0x10000 will start executing stage0 in the wrong function.

	Stage0 runs in low memory in 32-bit protected mode. It sets up temporary page tables to
	map high memory, and jumps to stage1 in high memory.
*/
extern void _stage0_init(uint16_t mem_kb, uint16_t pad0, uint32_t pad1, uint32_t pad2) {
	(void)pad0; (void)pad1; (void)pad2;
	set_up_stage0_page_tables();
	// Now we can access stuff in .text, which is mapped at +0xC0000000
	init_phys_mem_map(mem_kb);
	

	// Jump to higher half now
	// TODO: Not sure how I could omit this offset and things kept working after
	//       I discard identity mapping in kernel_main()?
	asm volatile("addl $0xC0000000, %%esp" : : : );
	kernel_main();
	asm volatile("cli; hlt");
}

struct phys_region mem_map[32] = { 0 };

uint32_t stage0_page_directory[1024] __attribute__((aligned(4096)));
uint32_t stage0_page_table1[1024] __attribute((aligned(4096)));

// stage0.S
extern void load_page_directory(phys_addr);
extern void enable_paging(void);

void set_up_stage0_page_tables(void) {
	for (int i = 0; i < 1024; ++i) {
		// r/w, only accessible by kernel, not present
		stage0_page_directory[i] = PD_READWRITE;
	}
	// Set up initial mapping, map 4MB starting from 0x10000, so
	// 0x10000-0x410000 -> 0xC0010000-0xC0410000
	// TODO: Really what I should be doing here is memcpying the kernel image
	// to where mem_map starts accounting, then mark those pages used up accordingly,
	// then continue with the page frame allocator from there.
	for (int i = 0; i < 1024; ++i) {
		stage0_page_table1[i] = ((i * PAGE_SIZE)) | 3;
		// page_table_ident[i] = (i * PAGE_SIZE) | 3;
	}

	// VGA buf
	// Seems I can comment this out and things still work. Learn why.
	//     ->Learned it now. The above 4MB mapping covers this address already.
	// page_table1[1023] = 0x000B8000 | 0x3;

	// 768 => 0xC0000000->
	stage0_page_directory[768] = (uint32_t)&stage0_page_table1[0] | 3;

	// Also identity map, for now.
	stage0_page_directory[0] = (uint32_t)&stage0_page_table1[0] | 3;
	// And map page directory, so we can still poke at it.
	stage0_page_directory[1023] = (uint32_t)&stage0_page_directory[0] | 3;
	load_page_directory((phys_addr)&stage0_page_directory[0]);
	enable_paging();
}
