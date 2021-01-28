#include <stdbool.h>
#include <stddef.h>
#include "stdint.h"

#include "terminal.h"
#include "assert.h"
#include "idt.h"
#include "mman.h"
 
#if defined(__linux__)
	#error "Cross compiler required, see toolchain/buildtoolchain.sh"
#endif
 
#if !defined(__i386__)
	#error "ix86-elf compiler required"
#endif

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

void kernel_main(uint32_t multiboot_magic, void *multiboot_header) {
	/* Initialize terminal interface */
	terminal_init(VGA_WIDTH, VGA_HEIGHT);
	
	validate_multiboot(multiboot_magic, multiboot_header);
	kprint("Hello!\n");
	idt_init();
	init_mman();
	kprint("Try and type something:\n");
	for (;;) {
		asm("hlt");
	}
}
