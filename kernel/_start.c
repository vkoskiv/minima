extern void kernel_main();
#include "mman.h"

void init_mman(uint16_t mem_kb);

/*
	Note: For this function to align at exactly 0x10000 in the final kernel image,
	it's actually important that this function is the first one in this file. Otherwise
	the bootloader blind jump to 0x10000 will start executing stage0 in the wrong function.
*/
// TODO: Rename this func & file to stage0, kernel.c/kernel_main to stage1
extern void _start(uint16_t mem_kb, uint16_t pad0, uint32_t pad1, uint32_t pad2) {
	(void)pad0; (void)pad1; (void)pad2;
	init_mman(mem_kb);

	// Jump to higher half now
	// TODO: Not sure how I could omit this offset and things kept working after
	//       I discard identity mapping in kernel_main()?
	asm volatile("addl $0xC0000000, %%esp" : : : );
	// TODO: Why does flushing here halt (but not crash) the system?
	// flush_cr3();
	kernel_main();
	asm volatile("cli; hlt");
}

struct phys_region {
	phys_addr start;
	uint32_t size;
};

struct phys_region mem_map[32] = { 0 };

// FIXME: Dynamic alloc with page frame allocator
uint32_t page_directory[1024] __attribute__((aligned(4096)));
uint32_t page_table1[1024] __attribute((aligned(4096)));
uint32_t *page_directory_base = &page_directory[0];
uint32_t *page_table_base = &page_table1[0];

// FIXME: Ignoring <1MB available memory for now.
// See linux arch/x86/kernel/e820.c, they patch in LOWMEMSIZE() and then later mark reserved bits.
void get_phys_mem_map(uint16_t mem_kb) {
	uint32_t mem_bytes = (uint32_t)mem_kb << 10;
	mem_map[0] = (struct phys_region){
		.start = 0x100000, .size = mem_bytes
	};
	mem_map[1] = (struct phys_region){ 0 };
}


uint32_t next_free_frame;

// paging.S
extern void load_page_directory(phys_addr);
extern void enable_paging(void);

void init_mman(uint16_t mem_kb) {
	get_phys_mem_map(mem_kb);
	for (int i = 0; i < 1024; ++i) {
		// r/w, only accessible by kernel, not present
		page_directory[i] = PD_READWRITE;
	}
	next_free_frame = 1;

	// Set up initial mapping, map 4MB starting from 0x10000, so
	// 0x10000-0x410000 -> 0xC0010000-0xC0410000
	// TODO: Really what I should be doing here is memcpying the kernel image
	// to where mem_map starts accounting, then mark those pages used up accordingly,
	// then continue with the page frame allocator from there.
	for (int i = 0; i < 1023; ++i) {
		page_table1[i] = ((i * PAGE_SIZE)) | 3;
		// page_table_ident[i] = (i * PAGE_SIZE) | 3;
	}

	// VGA buf
	// Seems I can comment this out and things still work. Learn why.
	//     ->Learned it now. The above 4MB mapping covers this address already.
	// page_table1[1023] = 0x000B8000 | 0x3;

	// 768 => 0xC0000000->
	page_directory[768] = (uint32_t)&page_table1[0] | 3;

	// Also identity map, for now.
	page_directory[0] = (uint32_t)&page_table1[0] | 3;
	// And map page directory, so we can still poke at it.
	page_directory[1023] = (uint32_t)&page_directory[0] | 3;
	load_page_directory((phys_addr)&page_directory[0]);
	enable_paging();
}
