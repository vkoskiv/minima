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

// TODO: pf_alloc() this
#define ARENASIZE (8*PAGE_SIZE)
uint8_t stage1_buf[ARENASIZE] = { 0 };

extern uint32_t stage0_page_table2;

struct pfcontainer {
	void *page;
	v_ilist linkage;
};

static void dump_arena_space_left(v_ma a) {
	uint32_t bytes_left = a.end - a.beg;
	kprintf("%iB left\n", bytes_left);
}

void stage1_init(void) {
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
	// arena.flags |= V_MA_SOFTFAIL;
	v_ma_on_oom(arena) {
		panic("stage1 arena OOM");
	}

	v_ilist pageframes = V_ILIST_INIT(pageframes);
	v_ilist freelist = V_ILIST_INIT(freelist);

	dump_phys_mem_stats(arena);
	kprintf("ESC = dump uptime\n1 = unmap identity\n2 = dump pd\n3 = free pf\n4 = show memory stats\n5 = allocate pf\n6 = toggle dark mode\n");
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
			v_ilist_prepend(&cont->linkage, &freelist);
			size_t allocd = v_ilist_count(&pageframes);
			kprintf("%i\n", allocd);
			dump_arena_space_left(arena);
		}
			break;
		case '4': {
			dump_phys_mem_stats(arena);
			size_t allocd = v_ilist_count(&pageframes);
			kprintf("%i allocated page frame%s\n", allocd, PLURAL(allocd));
		}
			break;
		case '5': {
			struct pfcontainer *cont;
			if (v_ilist_count(&freelist) && (cont = v_ilist_get_last(&freelist, struct pfcontainer, linkage)))
				v_ilist_remove(&cont->linkage);
			else
				cont = v_new(&arena, struct pfcontainer);
			if (!cont) {
				kprintf("arena and freelist full\n");
				break;
			}
			cont->page = pf_allocate();
			v_ilist_prepend(&cont->linkage, &pageframes);
			size_t allocd = v_ilist_count(&pageframes);
			kprintf("Allocated at %h -> %i\n", cont->page, allocd);
			dump_arena_space_left(arena);
		}
			break;
		case '6': {
			toggle_dark_mode();
		}
			break;
		default:
			kput(c);
			break;
		}
	}
}
