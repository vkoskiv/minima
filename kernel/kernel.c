#include <stdbool.h>
#include <stddef.h>
#include "stdint.h"

#include "terminal.h"
#include "assert.h"
#include "idt.h"
#include "mman.h"
#include "panic.h"
#include "utils.h"
#include "keyboard.h"
#include "serial_debug.h"
#include "timer.h"
#include "pfa.h"
 
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

extern void stage0_page_directory();

void dump_stage0_pd(void) {
	uint32_t *v_page_directory = (uint32_t *)(&stage0_page_directory + 0xC0000000);
	kprintf("stage0 page directory is at: %h\n", v_page_directory);
	for (int i = 0; i < 1024; ++i) {
		if (v_page_directory[i] > 2)
			kprintf("stage0_page_directory[%i]: %h\n", i, v_page_directory[i]);
	}
}

extern void pit_initialize(void);

void kernel_main(void) {
	terminal_init(VGA_WIDTH, VGA_HEIGHT);
	kbd_init();
	serial_setup();
	idt_init();
	pit_initialize();
	kprintf("Hello! Paging enabled, running in high memory.\n");
	uint32_t kernel_size = kernel_physical_end - kernel_physical_start;
	kprintf("%iK Kernel image at %h-%h\n", kernel_size, kernel_physical_start, kernel_physical_end);

	dump_phys_mem_stats();
	// for (int i = 0; i < 10; ++i) {
	// 	char *test = kmalloc(4096);
	// 	kprintf("kmalloc() returned: %h\n", test);
	// 	// memset(test, 'A', 4096);
	// }
	kprintf("ESC = dump uptime, 1 = unmap identity, 2 = dump pd, 3 = trigger read pf, 4 = trigger write pf\n");
	for (;;) {
		char c;
		while (read(&chardev_kbd, &c, 1) != 1)
			asm("hlt");
		switch (c) {
		case 0x1B: {
			uptime_t ut = get_uptime();
			kprintf("%iw %id %ih %im %is %ims        \r", ut.w, ut.d, ut.h, ut.m, ut.s, ut.ms);
		}
			break;
		case '1': {
			kprintf("Now unmapping identity.\n");
			uint32_t *v_page_directory = (uint32_t *)(&stage0_page_directory + 0xC0000000);
			v_page_directory[0] = 0x2;
			flush_cr3();
		}
			break;
		case '2':
			dump_stage0_pd();
			break;
		case '3': {
			char *bad = (char *)0xC4FEB4BE;
			char val = *bad;
			(void)val;
		}
			break;
		case '4': {
			char *bad = (char *)0xC4FEB4BE;
			*bad = 0x41;
		}
			break;
		case '5': {
			phys_addr page = pf_allocate();
			kprintf("Allocated at %h\n", page);
		}
			break;
		case '6':
			dump_phys_mem_stats();
			break;
		default:
			kput(c);
			break;
		}
	}
}
