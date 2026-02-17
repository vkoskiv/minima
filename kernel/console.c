#include "vkern.h"
#include "keyboard.h"
#include "mman.h"
#include "pfa.h"
#include "sched.h"
#include "assert.h"
#include "timer.h"

static void dump_arena_space_left(v_ma a) {
	uint32_t bytes_left = a.end - a.beg;
	kprintf("%iB left\n", bytes_left);
}

void recurse(int depth) {
	kprintf("depth: %i (%h)\n", depth, &depth);
	recurse(depth + 1);
}

void stress_kmalloc(void) {
	size_t bytes = 1;
	while (bytes <= 512 * MB) {
		void *buf = kmalloc(bytes);
		if (buf) {
			// kprintf("kmalloc(%i) -> %h\n", bytes, buf);
			char *b = buf;
			for (size_t i = 0; i < bytes; ++i) {
				b[i] = 0x41;
				assert(b[i] == 0x41);
			}
			kfree(buf);
		} else {
			// kprintf("kmalloc(%i) -> FAIL\n", bytes);
		}
		bytes = bytes * 2;
	}
}

static void dump_stage0_pd(void) {
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	kprintf("stage0 page directory is at: %h\n", page_directory);
	for (int i = 0; i < 1024; ++i) {
		if (page_directory[i] > 2)
			kprintf("\tstage0_pd[%i]: %h\n", i, page_directory[i]);
	}
}

static void dump_help(void) {
	kprintf(
	"ESC = dump uptime\n"
	"1 = dump pd\n"
	"2 = free pf\n"
	"3 = show memory stats\n"
	"4 = allocate pf\n"
	"5 = toggle dark mode\n"
	"6 = leak 64 pages\n"
	"7 = Blow the stack\n"
	"8 = stress kmalloc()\n"
	"9 = Spawn task\n"
	"o = Kill task\n"
	"t = List running tasks\n"
	"0 = show help\n");
}

struct pfcontainer {
	void *page;
	v_ilist linkage;
};

// Can't call into kput() yet, as there is no locking to prevent race conditions.
// Instead, write directly to fb for now
static uint16_t *vga_hackbuf = (uint16_t *)(VIRT_OFFSET + 0xB8000);

static const uint8_t s_bgcolor = 0x7;
static uint8_t s_fgcolor = 1;
#define SLEEP_MUL 100

static uint8_t s_x = 0;
static uint8_t s_y = 0;

static void task_func(void) {
	uint8_t x = s_x;
	uint8_t y = s_y;
	if (++s_x == 81) {
		s_x = 1;
		++s_y;
	}
	char c = 0x21;
	// uint16_t sleep_ms = SLEEP_MUL + (s_x * SLEEP_MUL);
	s_fgcolor++;
	if (s_fgcolor == 0x7)
		s_fgcolor++; // Skip grey-on-grey
	uint8_t color = (uint8_t)(s_fgcolor | s_bgcolor << 4);
	if (s_fgcolor > 15)
		s_fgcolor = 1;
	while (1) {
		sleep(1);
		// sleep(sleep_ms/2);
		// void *buf = kmalloc(PAGE_SIZE);
		vga_hackbuf[y * VGA_WIDTH + x] = ((uint16_t)c++ & 0xff) | (uint16_t)color<<8;
		if (c >= 0x7E)
			c = 0x21;
		// sleep(sleep_ms/2);
		// kfree(buf);
	}
}

#define CONSOLE_BUF_SIZE (1 * MB)

void console_task(void) {
	uint8_t *console_buf = kmalloc(CONSOLE_BUF_SIZE);
	v_ma arena = v_ma_from_buf(console_buf, CONSOLE_BUF_SIZE);
	// arena.flags |= V_MA_SOFTFAIL;
	v_ma_on_oom(arena) {
		panic("console arena OOM");
	}

	v_ilist pageframes = V_ILIST_INIT(pageframes);
	v_ilist freelist = V_ILIST_INIT(freelist);
	// for (;;) {
	// 	uptime_t ut = get_uptime();
	// 	kprintf("%iw %id %ih %im %is %ims        \r", ut.w, ut.d, ut.h, ut.m, ut.s, ut.ms);
	// 	sleep(1000);
	// }

	tid_t *tasks = v_new(&arena, tid_t, MAX_TASKS);
	tasks[0] = 0;
	tasks[1] = current->id;
	uint32_t tasks_idx = 2;
	dump_phys_mem_stats(arena);
	dump_help();
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
			dump_vm_ranges("vm_regions");
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
		case '8': {
			stress_kmalloc();
		}
			break;
		case '9': {
			tid_t ret = task_create(task_func, "task_func");
			if (ret < 0)
				kprintf("No more task slots\n");
			else {
				tasks[tasks_idx] = ret;
				kprintf("task_create() -> %i\n", tasks[tasks_idx++]);
			}
		}
			break;
		case 'o': {
			if (tasks[tasks_idx - 1] == current->id)
				kprintf("Won't kill console task (%i)\n", current->id);
			else {
				int ret = task_kill(tasks[tasks_idx - 1]);
				if (ret < 0)
					kprintf("Failed to kill task %i\n", tasks[tasks_idx - 1]);
				else {
					kprintf("task_kill(%i) -> %i\n", tasks[--tasks_idx], ret);
				}
			}
		}
			break;
		case 't':
			dump_running_tasks();
			break;
		case '0':
			dump_help();
			break;
		default:
			kput(c);
			break;
		}
	}
}
