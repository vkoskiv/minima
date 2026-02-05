extern void kernel_main();
#include "mman.h"

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
