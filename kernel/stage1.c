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

void dump_stage0_pd(void) {
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	kprintf("stage0 page directory is at: %h\n", page_directory);
	for (int i = 0; i < 1024; ++i) {
		if (page_directory[i] > 2)
			kprintf("\tstage0_pd[%i]: %h\n", i, page_directory[i]);
	}
}

extern void pit_initialize(void);

struct pfcontainer {
	void *page;
	v_ilist linkage;
};

static void dump_arena_space_left(v_ma a) {
	uint32_t bytes_left = a.end - a.beg;
	kprintf("%iB left\n", bytes_left);
}

void recurse(int depth) {
	kprintf("depth: %i (%h)\n", depth, &depth);
	recurse(depth + 1);
}

void stage1_init(void) {
	terminal_init(VGA_WIDTH, VGA_HEIGHT);
	kbd_init();
	serial_setup();
	idt_init();
	pit_initialize();
	pfa_init();
	kprintf("Hello! Paging enabled, running in high memory.\n");
	kprintf("Now unmapping identity.\n");
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	page_directory[0] = 0x2;
	flush_cr3();
	uint32_t kernel_bytes = kernel_physical_end - kernel_physical_start;
	kprintf("Kernel image at %h-%h (%ik, %i pages)\n", kernel_physical_start, kernel_physical_end,
	        kernel_bytes / 1024, PAGE_ROUND_UP(kernel_bytes) / PAGE_SIZE);

	uint8_t *stage1_buf = pf_alloc();
	v_ma arena = v_ma_from_buf(stage1_buf, PAGE_SIZE);
	// arena.flags |= V_MA_SOFTFAIL;
	v_ma_on_oom(arena) {
		panic("stage1 arena OOM");
	}

	v_ilist pageframes = V_ILIST_INIT(pageframes);
	v_ilist freelist = V_ILIST_INIT(freelist);

	dump_phys_mem_stats(arena);
	kprintf("ESC = dump uptime\n1 = dump pd\n2 = free pf\n3 = show memory stats\n4 = allocate pf\n5 = toggle dark mode\n6 = leak 64 pages\n7 = Blow the stack\n");
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
		case '1':
			dump_stage0_pd();
			break;
		case '2': {
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
		case '3': {
			dump_phys_mem_stats(arena);
			size_t allocd = v_ilist_count(&pageframes);
			kprintf("%i allocated page frame%s\n", allocd, PLURAL(allocd));
		}
			break;
		case '4': {
			struct pfcontainer *cont;
			void *page = pf_alloc();
			if (!page)
				break;
			if (v_ilist_count(&freelist) && (cont = v_ilist_get_last(&freelist, struct pfcontainer, linkage)))
				v_ilist_remove(&cont->linkage);
			else
				cont = v_new(&arena, struct pfcontainer);
			if (!cont) {
				kprintf("arena and freelist full\n");
				break;
			}
			cont->page = page;
			v_ilist_prepend(&cont->linkage, &pageframes);
			size_t allocd = v_ilist_count(&pageframes);
			kprintf("Allocated at %h -> %i\n", cont->page, allocd);
			dump_arena_space_left(arena);
		}
			break;
		case '5': {
			toggle_dark_mode();
		}
			break;
		case '6': {
			void *pf;
			for (size_t i = 0; i < 64; ++i) {
				void *next = pf_alloc();
				if (!next)
					break;
				pf = next;
				if (!i)
					kprintf("Leaking pages %h-", pf);
			}
			kprintf("%h\n", pf);
			dump_phys_mem_stats(arena);
		}
			break;
		case '7': {
			int depth = 0;
			recurse(depth);
		}
			break;
		default:
			kput(c);
			break;
		}
	}
}
