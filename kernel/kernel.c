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
#include "keyboard.h"
#include "serial_debug.h"
 
#if defined(__linux__)
	#error "Cross compiler required, see toolchain/buildtoolchain.sh"
#endif
 
#if !defined(__i386__)
	#error "ix86-elf compiler required"
#endif

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

void _asm_gdt_descriptor();
phys_addr asm_gdt_descriptor = (phys_addr)&_asm_gdt_descriptor;
extern phys_addr kernel_physical_start;
extern phys_addr kernel_physical_end;

// From boot.s
void discard_identity(void);

extern void page_directory();

void dump_page_directory(void) {
	uint32_t *v_page_directory = (uint32_t *)(&page_directory + 0xC0000000);
	kprintf("v_page_directory is at: %h\n", v_page_directory);
	for (int i = 0; i < 1024; ++i) {
		if (v_page_directory[i] > 2)
			kprintf("v_page_directory[%i]: %h\n", i, v_page_directory[i]);
	}
}

void kernel_main(void) {
	terminal_init(VGA_WIDTH, VGA_HEIGHT);
	kbd_init();
	serial_setup();
	idt_init();
	dump_phys_regions();
	kprintf("Hello! Paging enabled, running in high memory.\n");
	kprintf("Address of kernel entry point: %h\n", kernel_main);
	kprintf("That's it for now!\n");
	kprintf("kernel_physical_start = %h\n", (void *)kernel_physical_start);
	kprintf("kernel_physical_end = %h\n", (void *)kernel_physical_end);

	dump_page_directory();

	// bx_dbg_read_linear: physical address not available for linear 0x00003ff0
	// No idea why my page fault handler doesn't get called?
	// kprintf("Now unmapping identity.\n");
	// uint32_t *v_page_directory = (uint32_t *)(&page_directory + 0xC0000000);
	// v_page_directory[0] = 0x2;
	// flush_cr3();
	
	// for (int i = 0; i < 10; ++i) {
	// 	char *test = kmalloc(4096);
	// 	kprintf("kmalloc() returned: %h\n", test);
	// 	// memset(test, 'A', 4096);
	// }
	kprintf("Try and type something:\n");
	for (;;)
		asm("hlt");
}
