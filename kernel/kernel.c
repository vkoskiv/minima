#include <stdbool.h>
#include <stddef.h>
#include "stdint.h"

#include "terminal.h"
#include "assert.h"
#include "idt.h"
#include "mman.h"
#include "multiboot.h"
#include "panic.h"
#include "utils.h"
 
#if defined(__linux__)
	#error "Cross compiler required, see toolchain/buildtoolchain.sh"
#endif
 
#if !defined(__i386__)
	#error "ix86-elf compiler required"
#endif

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

// From boot.s
void discard_identity(void);

// From gdt.s
void *asm_gdt_descriptor;
void asm_gdt_init(void);

void kernel_main(uint32_t multiboot_magic, void *multiboot_info) {
	/* Initialize terminal interface */
	terminal_init(VGA_WIDTH, VGA_HEIGHT);
	kprintf("Hello! Paging enabled, running in high memory.\n");
	kprintf("Address of kernel entry point: %h\n", kernel_main);
	struct multiboot_info *info = validate_multiboot(multiboot_magic, multiboot_info);

	kprintf("Loading GDT at address %h\n", asm_gdt_descriptor);
	asm_gdt_init();
	idt_init();
	init_mman(info);
	kprintf("Now unmapping identity.\n");
	discard_identity();
	// dump_page_directory();
	
	// for (int i = 0; i < 10; ++i) {
	// 	char *test = kmalloc(4096);
	// 	kprintf("kmalloc() returned: %h\n", test);
	// 	// memset(test, 'A', 4096);
	// }
	kprintf("Try and type something:\n");
	for (;;) {
		asm("hlt");
	}
}
