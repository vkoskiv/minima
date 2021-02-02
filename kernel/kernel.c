#include <stdbool.h>
#include <stddef.h>
#include "stdint.h"

#include "terminal.h"
#include "assert.h"
#include "idt.h"
#include "mman.h"
#include "multiboot.h"
 
#if defined(__linux__)
	#error "Cross compiler required, see toolchain/buildtoolchain.sh"
#endif
 
#if !defined(__i386__)
	#error "ix86-elf compiler required"
#endif

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

extern uint32_t _kernel_start;
extern uint32_t _kernel_end;

// From boot.s
void discard_identity(void);

#include "io.h"
#include "panic.h"
void kernel_main(uint32_t multiboot_magic, void *multiboot_header) {
	/* Initialize terminal interface */
	terminal_init(VGA_WIDTH, VGA_HEIGHT);
	kprint("Paging enabled, running in high memory.\n");
	kprint("Address of kernel entry point: ");
	kprintaddr((void *)kernel_main);
	kprint("\n");
	//panic();
	validate_multiboot(multiboot_magic, multiboot_header);
	
	//init_mman(multiboot_header);
	kprint("Hello!\n");
	//kprint("Now unmapping identity.\n");
	//discard_identity();
	gdt_idt_init();
	
	
	kprint("Try and type something:\n");
	for (;;) {
		asm("hlt");
	}
}
