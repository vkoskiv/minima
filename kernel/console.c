#include "vkern.h"
#include "keyboard.h"
#include "mman.h"
#include "pfa.h"
#include "sched.h"
#include "timer.h"
#include "assert.h"

static void recurse_slow(int depth) {
	sleep(1);
	recurse_slow(depth + 1);
}

static void stack_overflow_gentle(void) {
	recurse_slow(0);
}

static void recurse_fast(int depth) {
	recurse_fast(depth + 1);
}

// FIXME: Easily blows stack in a single time slice
static void stack_overflow_hard(void) {
	recurse_fast(0);
}

static void dump_stage0_pd(void) {
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	kprintf("stage0 page directory is at: %h\n", page_directory);
	for (int i = 0; i < 1024; ++i) {
		if (page_directory[i] > 2)
			kprintf("\tstage0_pd[%i]: %h\n", i, page_directory[i]);
	}
}

// Can't call into kput() yet, as there is no locking to prevent race conditions.
// Instead, write directly to fb for now
static uint16_t *vga_hackbuf = (uint16_t *)(VIRT_OFFSET + 0xB8000);

static const uint8_t s_bgcolor = 0x7;
static uint8_t s_fgcolor = 1;
#define SLEEP_MUL 10

#define DX 79
#define DY 25
static uint16_t spot_idx = 0;

/*
	Bunch of different sized allocs simultaneously in random order.
	Triggers PANIC pretty quick:
		PANIC: vmfree[mman.c:216]: Attempted to free unknown vma
*/
static void kmalloc_stress(void) {
	uint8_t x = DX - (spot_idx / DY);
	uint8_t y = (spot_idx % DY);
	// spot_idx++;
	char c = 0x21;
	uint16_t sleep_ms = SLEEP_MUL + (spot_idx++ * SLEEP_MUL);
	s_fgcolor++;
	if (s_fgcolor == 0x7)
		s_fgcolor++; // Skip grey-on-grey
	uint8_t color = (uint8_t)(s_fgcolor | s_bgcolor << 4);
	if (s_fgcolor > 15)
		s_fgcolor = 1;
	while (1) {
		// sleep(1);
		sleep(sleep_ms/2);
		void *buf = kmalloc(spot_idx * PAGE_SIZE);
		if (!buf) {
			kprintf("kmstress %i failed to allocate %ib\n", current->id, spot_idx * PAGE_SIZE);
			while (1)
				sleep(1000);
		}
		vga_hackbuf[y * VGA_WIDTH + x] = ((uint16_t)c++ & 0xff) | (uint16_t)color<<8;
		if (c >= 0x7E)
			c = 0x21;
		sleep(sleep_ms/2);
		kfree(buf);
	}
}

static void vga_flasher(void) {
	uint8_t x = DX - (spot_idx / DY);
	uint8_t y = (spot_idx % DY);
	spot_idx++;
	char c = 0x21;
	// uint16_t sleep_ms = SLEEP_MUL + (spot_idx++ * SLEEP_MUL);
	s_fgcolor++;
	if (s_fgcolor == 0x7)
		s_fgcolor++; // Skip grey-on-grey
	uint8_t color = (uint8_t)(s_fgcolor | s_bgcolor << 4);
	if (s_fgcolor > 15)
		s_fgcolor = 1;
	while (1) {
		sleep(1);
		// sleep(sleep_ms);
		vga_hackbuf[y * VGA_WIDTH + x] = ((uint16_t)c++ & 0xff) | (uint16_t)color<<8;
		if (c >= 0x7E)
			c = 0x21;
	}
}

void clock_task(void) {
	for (;;) {
		uptime_t ut = get_uptime();
		kprintf("\r%id %ih %im %is > ", ut.d, ut.h, ut.m, ut.s);
		sleep(100);
	}
}
static tid_t clock_tid = -1;

static void dump_mem_stats(void) {
	while(1) {
		dump_vm_ranges("vm_regions");
		uint8_t buf[256];
		v_ma temp = v_ma_from_arr(buf);
		dump_phys_mem_stats(temp);
		sleep(1000);
	}
}

static void leak_64(void) {
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
	uint8_t buf[256];
	v_ma temp = v_ma_from_arr(buf);
	dump_phys_mem_stats(temp);
}

struct tidbox {
	tid_t tid;
	v_ilist cmd;
	v_ilist tasks;
};

static void dump_help(void);
#define TASK(task_entry) (task_entry), #task_entry
struct cmd {
	v_ilist tids;
	int max_tids;
	void (*task_entry)(void);
	const char *name;
	const char *descr;
	const char shortcut_spawn;
	const char shortcut_kill;
};
//        v-- 0 == runs in console_task, <0 == unlimited
static struct cmd cmds[] = {
	{ {},  0, TASK(dump_stage0_pd), "dump pd",                      '1',  0  },
	{ {},  1, TASK(dump_mem_stats), "show memory stats",            '2', 'w' },
	{ {},  0, TASK(toggle_dark_mode), "toggle dark mode",           '3',  0  },
	{ {},  0, TASK(leak_64), "leak 64 pages",                       '4',  0  },
	{ {}, -1, TASK(stack_overflow_gentle), "Blow the stack gently", '5', 't' },
	{ {}, -1, TASK(stack_overflow_hard), "Blow the stack hard",     '6', 'y' },
	{ {}, -1, TASK(kmalloc_stress), "stress kmalloc()",             '7', 'u' },
	{ {}, -1, TASK(vga_flasher), "VGA flasher task",                '8', 'i' },
	{ {},  0, TASK(dump_running_tasks), "List running tasks",       '9',  0  },
	{ {},  0, TASK(dump_help), "show help",                         '0',  0  },
};

static void dump_help(void) {
	for (size_t i = 0; i < (sizeof(cmds) / sizeof(cmds[0])); ++i) {
		if (cmds[i].shortcut_kill)
			kprintf("[%c/%c]", cmds[i].shortcut_spawn, cmds[i].shortcut_kill);
		else
			kprintf("[%c]", cmds[i].shortcut_spawn);
		kprintf(" %s: %s\n", cmds[i].name, cmds[i].descr);
	}
}

#define CONSOLE_BUF_SIZE (1 * MB)

static int spawn_or_run(v_ma *a, v_ilist *tasks, v_ilist *tasks_free, struct cmd *cmd) {
	if (cmd->max_tids == 0) {
		cmd->task_entry();
		return 0;
	}
	if (cmd->max_tids > 0 && (int)v_ilist_count(&cmd->tids) > cmd->max_tids)
		return -1;

	tid_t ret = task_create(cmd->task_entry, cmd->name);
	if (ret < 0)
		return -1;
	struct tidbox *b = NULL;
	// FIXME v.h: This is quite awkward, and already starting to show up repeatedly.
	if (v_ilist_count(tasks_free)) {
		b = v_ilist_get_first(tasks_free, struct tidbox, tasks);
		v_ilist_remove(&b->tasks);
	} else
		b = v_new(a, struct tidbox);
	b->tid = ret;
	v_ilist_prepend(&b->cmd, &cmd->tids);
	v_ilist_prepend(&b->tasks, tasks);
	return b->tid;
}

static int kill_or_nah(v_ilist *tasks_free, struct cmd *cmd) {
	if (cmd->max_tids == 0)
		return -1;
	if (!v_ilist_count(&cmd->tids))
		return -1;
	struct tidbox *b = v_ilist_get_last(&cmd->tids, struct tidbox, cmd);
	int ret = task_kill(b->tid);
	if (ret < 0) {
		kprintf("Failed to kill tid %i of task %s\n", b->tid, cmd->name);
		return -1;
	}
	v_ilist_remove(&b->cmd);
	v_ilist_remove(&b->tasks);
	v_ilist_prepend(&b->tasks, tasks_free);
	return ret;
}

void console_task(void) {
	uint8_t *console_buf = kmalloc(CONSOLE_BUF_SIZE);
	v_ma arena = v_ma_from_buf(console_buf, CONSOLE_BUF_SIZE);
	// arena.flags |= V_MA_SOFTFAIL;
	v_ma_on_oom(arena) {
		panic("console arena OOM");
	}
	V_ILIST(tasks);
	V_ILIST(tasks_free);
	for (size_t i = 0; i < (sizeof(cmds) / sizeof(cmds[0])); ++i)
		cmds[i].tids = V_ILIST_INIT(cmds[i].tids);
	dump_phys_mem_stats(arena);
	kprintf("0 = help\n");

	if (clock_tid < 0)
		clock_tid = task_create(clock_task, "clock_task");

	for (;;) {
		char c;
		while (read(&chardev_kbd, &c, 1) != 1)
			asm("hlt");
		for (size_t i = 0; i < (sizeof(cmds) / sizeof(cmds[0])); ++i) {
			if (c == cmds[i].shortcut_spawn) {
				if (cmds[i].max_tids == 0)
					kprintf("%s()\n", cmds[i].name);
				else
					kprintf("spawn(%s)", cmds[i].name);
				int ret = spawn_or_run(&arena, &tasks, &tasks_free, &cmds[i]);
				if (ret < 0)
					kprintf(" -> Failed (%i)\n", ret);
				else if (ret > 0)
					kprintf(" -> %i\n", ret);
				break;
			}
			if (c == cmds[i].shortcut_kill) {
				kprintf("kill(%s)", cmds[i].name);
				int ret = kill_or_nah(&tasks_free, &cmds[i]);
				if (ret < 0)
					kprintf(" -> Failed (%i)\n", ret);
				else
					kprintf(" -> %i\n", ret);
				break;
			}
		}
	}
}
