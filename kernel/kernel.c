#include <stdbool.h>
#include <stddef.h>
#include "stdint.h"
#include <vkern.h>
#include "terminal.h"
#include "idt.h"
#include "mman.h"
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

extern void stage0_page_directory();

void dump_stage0_pd(void) {
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	kprintf("stage0 page directory is at: %h\n", page_directory);
	for (int i = 0; i < 1024; ++i) {
		if (page_directory[i] > 2)
			kprintf("\tstage0_pd[%i]: %h\n", i, page_directory[i]);
	}
}

extern void pit_initialize(void);

#define ARENASIZE (8*PAGE_SIZE)
uint8_t stage1_buf[ARENASIZE] = { 0 };

extern uint32_t stage0_page_table2;

struct pfcontainer {
	void *page;
	v_ilist linkage;
};

void kernel_main(void) {
	terminal_init(VGA_WIDTH, VGA_HEIGHT);
	kbd_init();
	serial_setup();
	idt_init();
	pit_initialize();
	pfa_init();
	kprintf("Hello! Paging enabled, running in high memory.\n");
	uint32_t kernel_size = kernel_physical_end - kernel_physical_start;
	kprintf("%iK Kernel image at %h-%h\n", kernel_size, kernel_physical_start, kernel_physical_end);

	v_ma arena = v_ma_from_buf(stage1_buf, ARENASIZE);

	v_ilist pageframes = V_ILIST_INIT(pageframes);

	dump_phys_mem_stats(arena);
	kprintf("ESC = dump uptime\n1 = unmap identity\n2 = dump pd\n3 = free pf\n4 = show memory stats\n5 = allocate pf\n");
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
			if (!v_ilist_count(&pageframes))
				break;
			struct pfcontainer *cont = v_ilist_get_last(&pageframes, struct pfcontainer, linkage);
			kprintf("Freeing at %h -> ", cont->page);
			pf_free(cont->page);
			v_ilist_remove(&cont->linkage);
			size_t allocd = v_ilist_count(&pageframes);
			kprintf("%i\n", allocd);
		}
			break;
		case '4': {
			dump_phys_mem_stats(arena);
			size_t allocd = v_ilist_count(&pageframes);
			kprintf("%i allocated page frame%s\n", allocd, PLURAL(allocd));
		}
			break;
		case '5': {
			void *page = pf_allocate();
			struct pfcontainer *cont = v_new(&arena, struct pfcontainer);
			cont->page = page;
			v_ilist_prepend(&cont->linkage, &pageframes);
			size_t allocd = v_ilist_count(&pageframes);
			kprintf("Allocated at %h -> %i\n", page, allocd);
		}
			break;
		default:
			kput(c);
			break;
		}
	}
}
