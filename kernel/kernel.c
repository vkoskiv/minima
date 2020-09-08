#include <stdbool.h>
#include <stddef.h>
#include "stdint.h"

#include "terminal.h"
 
#if defined(__linux__)
	#error "Cross compiler required, see toolchain/buildtoolchain.sh"
#endif
 
#if !defined(__i386__)
	#error "ix86-elf compiler required"
#endif
 

 
static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
 
void kernel_main(void) {
	/* Initialize terminal interface */
	terminal_initialize(VGA_WIDTH, VGA_HEIGHT);
 
	tprint("Hello, kernel World!\n");
	tprint("This is text from the minima kernel.\n");
	tprint("Cool!\n");

	tprint("\nHow cool is THIS?!\n");
}
