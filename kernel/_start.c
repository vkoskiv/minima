extern void kernel_main();
#include "mman.h"

extern void _start() {
	init_mman();

	// Jump to higher half now
	kernel_main();
	asm volatile("cli; hlt");
}
