#include <console.h>
#include <keyboard.h>
#include <mm/vma.h>
#include <mm/pfa.h>
#include <kmalloc.h>
#include <sched.h>
#include <timer.h>
#include <assert.h>
#include <syscalls.h>
#include <kprintf.h>
#include <fs/dev_block.h>

static void recurse_slow(int depth) {
	sleep(1);
	recurse_slow(depth + 1);
}

static int stack_overflow_gentle(void *ctx) {
	(void)ctx;
	recurse_slow(0);
	return 0;
}

static void recurse_fast(int depth) {
	recurse_fast(depth + 1);
}

// FIXME: Easily blows stack in a single time slice
// NOTE: I read somewhere that there is a flag that one can enable
// to also check page writes in kernel mode? But I can't find it now, could
// be that it's a newer feature.
static int stack_overflow_hard(void *ctx) {
	(void)ctx;
	recurse_fast(0);
	return 0;
}

static int dump_stage0_pd(void *ctx) {
	(void)ctx;
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	kprintf("stage0 page directory is at: %h\n", page_directory);
	for (int i = 0; i < 1024; ++i) {
		if (page_directory[i] > 2)
			kprintf("\tstage0_pd[%i]: %h\n", i, page_directory[i]);
	}
	return 0;
}

// Can't call into kput() yet, as there is no locking to prevent race conditions.
// Instead, write directly to fb for now
static uint16_t *vga_hackbuf = (uint16_t *)(VIRT_OFFSET + 0xB8000);

static const uint8_t s_bgcolor = 0x7;
static uint8_t s_fgcolor = 1;
#define SLEEP_MUL 10

#define DX 79
#define DY 25

// Hack. I can't pass signals to these things yet, so I'll just increment
// this variable and detect that in the loop of these tests
static volatile uint32_t kill_sig = 0;
static int kmalloc_stress(void *ctx) {
	uint32_t *idx = ctx;
	uint32_t spot_idx = *idx;
	uint32_t alloc_mag = (spot_idx % 7); // FIXME: mags 1024, 2048 and 4096 are horrible for perf, making too many slabs
	kprintf("kmalloc_stress with alloc of %u bytes\n", 0x8 << alloc_mag);
	(*idx)++;
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

	uint8_t rand_sleep = 0;
	uint16_t rand_count = 0;
	uint16_t flip = 0;

	void *allocs[256];
	uint32_t orig_kill = kill_sig;

	while (1) {
		// sleep(100);
		rand_count = (((system_uptime_ms >> 8) | system_uptime_ms) & 0xFFFF) % 255;
		rand_sleep = ((rand_count % 16)/2) + 1;
		flip = (rand_count | rand_sleep) > 128 ? 1 : 0;
		// kprintf("sleep %u %u, count: %u, flip: %u\n", rand_sleep, rand_sleep, rand_count, flip);

		for (size_t i = 0; i < rand_count; ++i)
			allocs[i] = kmalloc(0x8 << alloc_mag);

		sleep(rand_sleep);

		vga_hackbuf[y * VGA_WIDTH + x] = ((uint16_t)c++ & 0xff) | (uint16_t)color<<8;
		if (c >= 0x7E)
			c = 0x21;
		// sleep(sleep_ms/2);
		if (flip) {
			// FIXME: actually flip this
			for (size_t i = 0; i < rand_count; ++i)
				kfree(allocs[i]);
		} else {
			for (size_t i = 0; i < rand_count; ++i)
				kfree(allocs[i]);
		}

		if (kill_sig != orig_kill)
			break;

		// sleep(1);
		// sleep(sleep_ms/2);
		// void *buf = kmalloc(0x8 << alloc_mag);
		// if (!buf) {
		// 	kprintf("kmstress %i failed to allocate %ub\n", current->id, 0x8 << alloc_mag);
		// 	while (1)
		// 		sleep(1000);
		// }
		// vga_hackbuf[y * VGA_WIDTH + x] = ((uint16_t)c++ & 0xff) | (uint16_t)color<<8;
		// if (c >= 0x7E)
		// 	c = 0x21;
		// // sleep(sleep_ms/2);
		// kfree(buf);
	}
	return 0;
}

static int vga_flasher(void *ctx) {
	uint32_t *idx = ctx;
	uint32_t spot_idx = *idx;
	(*idx)++;
	uint8_t x = DX - (spot_idx / DY);
	uint8_t y = (spot_idx % DY);
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
	return 0;
}

int clock_task(void *ctx) {
	(void)ctx;
	for (;;) {
		uptime_t ut = get_uptime();
		kprintf("\r%id %ih %im %is > ", ut.d, ut.h, ut.m, ut.s);
		sleep(100);
	}
	return 0;
}
static tid_t clock_tid = -1;

static int dump_mem_stats(void *ctx) {
	(void)ctx;
	dump_vm_ranges("vm_regions");
	uint8_t buf[256];
	v_ma temp = v_ma_from_arr(buf);
	dump_phys_mem_stats(temp);
	return 0;
}

static int dump_tasks(void *ctx) {
	(void)ctx;
	dump_running_tasks();
	return 0;
}

struct tidbox {
	tid_t tid;
	v_ilist cmd;
	v_ilist tasks;
};

#define CONSOLE_BUF_SIZE (4 * PAGE_SIZE)

static int spawn_or_run(v_ma *a, v_ilist *tasks, v_ilist *tasks_free, struct cmd *cmd) {
	if (cmd->shortcut_kill && cmd->max_tids > 0 && (int)v_ilist_count(&cmd->tids) >= cmd->max_tids)
		return -1;

	tid_t ret = task_create(cmd->task_entry, cmd->ctx, cmd->name, cmd->is_user);
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

uint32_t spot_idx = 0;

int u_hello1(void *ctx) {
	int arg1 = (int)ctx;
	SYSCALL1(SYS_HELLO1, arg1);
	SYSCALL1(SYS_EXIT, 0);
	assert(NORETURN);
	return 0;
}

int u_hello2(void *ctx) {
	int arg1 = (int)ctx;
	int arg2 = (int)ctx + 1;
	SYSCALL2(SYS_HELLO2, arg1, arg2);
	SYSCALL1(SYS_EXIT, 0);
	assert(NORETURN);
	return 0;
}

int u_hello3(void *ctx) {
	int arg1 = (int)ctx;
	int arg2 = (int)ctx + 1;
	int arg3 = (int)ctx + 2;
	SYSCALL3(SYS_HELLO3, arg1, arg2, arg3);
	SYSCALL1(SYS_EXIT, 0);
	assert(NORETURN);
	return 0;
}

int u_hello4(void *ctx) {
	int arg1 = (int)ctx;
	int arg2 = (int)ctx + 1;
	int arg3 = (int)ctx + 2;
	int arg4 = (int)ctx + 3;
	SYSCALL4(SYS_HELLO4, arg1, arg2, arg3, arg4);
	SYSCALL1(SYS_EXIT, 0);
	assert(NORETURN);
	return 0;
}

int u_hello5(void *ctx) {
	int arg1 = (int)ctx;
	int arg2 = (int)ctx + 1;
	int arg3 = (int)ctx + 2;
	int arg4 = (int)ctx + 3;
	int arg5 = (int)ctx + 4;
	SYSCALL5(SYS_HELLO5, arg1, arg2, arg3, arg4, arg5);
	SYSCALL1(SYS_EXIT, 0);
	assert(NORETURN);
	return 0;
}

int u_hello6(void *ctx) {
	int arg1 = (int)ctx;
	int arg2 = (int)ctx + 1;
	int arg3 = (int)ctx + 2;
	int arg4 = (int)ctx + 3;
	int arg5 = (int)ctx + 4;
	int arg6 = (int)ctx + 5;

	SYSCALL6(SYS_HELLO6, arg1, arg2, arg3, arg4, arg5, arg6);

	SYSCALL1(SYS_EXIT, 0);
	assert(NORETURN);
	return 0;
}

int u_sleep(void *ctx) {
	(void)ctx;
	int val = 0;
	const int sleep_ms = 100;
	while (1) {
		SYSCALL1(SYS_HELLO1, val++);
		SYSCALL1(SYS_SLEEP, sleep_ms);
	}
}

static struct cmd_list user_tests = {
	.name = "user_tests",
	.cmds = {
		{ {}, 1, -1, NULL,      TASK(u_hello1), "Spawn user task calling sys$hello1", 'a', 'z' },
		{ {}, 1, -1, NULL,      TASK(u_hello2), "Spawn user task calling sys$hello2", 's', 'x' },
		{ {}, 1, -1, NULL,      TASK(u_hello3), "Spawn user task calling sys$hello3", 'd', 'c' },
		{ {}, 1, -1, NULL,      TASK(u_hello4), "Spawn user task calling sys$hello4", 'f', 'v' },
		{ {}, 1, -1, NULL,      TASK(u_hello5), "Spawn user task calling sys$hello5", 'g', 'b' },
		{ {}, 1, -1, NULL,      TASK(u_sleep),  "Spawn user task to test sys$sleep",  'h', 'n' },
		{ 0 },
	}
};

static int test_kprintf(void *ctx) {
	uint8_t foo1 = 0x12;
	uint16_t foo2 = 0x1234;
	uint32_t foo3 = 0x123456;
	uint32_t foo4 = 0x12345678;
	kprintf("%h %h %h %h\n", foo1, foo2, foo3, foo4);
	kprintf("%1h %2h %3h %4h\n", foo1, foo2, foo3, foo4);
	kprintf("%4h %3h %2h %1h\n", foo1, foo2, foo3, foo4);
	const char *string = "0123456789ABCDEF";
	kprintf("%%s: %s\n", string);
	kprintf("%%1s: %1s\n", string);
	kprintf("%%2s: %2s\n", string);
	kprintf("%%3s: %3s\n", string);
	kprintf("%%4s: %4s\n", string);
	kprintf("%%5s: %5s\n", string);
	kprintf("%%128s: %128s\n", string);
	const char *bee_movie = "Bee Movie Script According to all known laws of aviation, there is no way a bee should be able to fly. Its wings are too small to get its fat little body off the ground. The bee, of course, flies anyway because bees don't care what humans think is impossible. Yellow, black. Yellow, black. Yellow, black. Yellow, black. Ooh, black and yellow! Let's shake it up a little. Barry!";
	kprintf("strlen(bee_movie) == %u\n", strlen(bee_movie));
	kprintf("%%300s: %300s\n", bee_movie);
	kprintf("%%500s: %500s\n", bee_movie);
	return 0;
}

static struct cmd_list kpftest = {
	.name = "kprintftest",
	.cmds = {
		{ {}, 0,  1, NULL,      TASK(test_kprintf), "run kprintf test",               'q',  0  },
		{ 0 },
	}
};

static int dump_help(void *ctx) {
	struct cmd_list *list = ctx;
	struct cmd *cmds = list->cmds;
	for (size_t i = 0; cmds[i].task_entry && cmds[i].name; ++i) {
		if (cmds[i].shortcut_kill)
			kprintf("[%c/%c]", cmds[i].shortcut_spawn, cmds[i].shortcut_kill);
		else
			kprintf("[%c]", cmds[i].shortcut_spawn);
		kprintf(" %s: %s\n", cmds[i].name, cmds[i].descr);
	}
	return 0;
}

int enter_cmdlist(void *ctx) {
	struct cmd_list *list = ctx;
	struct cmd *cmds = list->cmds;
	const char *name = list->name;
	uint8_t *console_buf = kmalloc(CONSOLE_BUF_SIZE);
	v_ma arena = v_ma_from_buf(console_buf, CONSOLE_BUF_SIZE);
	v_ma_on_oom(arena) {
		panic("%s arena OOM", name);
	}
	struct dev_char *keyboard = dev_char_open("keyboard");
	assert(keyboard);
	V_ILIST(tasks);
	V_ILIST(tasks_free);
	for (size_t i = 0; cmds[i].task_entry && cmds[i].name; ++i)
		cmds[i].tids = V_ILIST_INIT(cmds[i].tids);
	kprintf("Entered %s. Press 0 for help, ESC to exit.\n", name);

	char c;
	int eaten = 1;
	for (;;) {
		if (eaten) {
			kprintf("%s> ", name);
		} else {
			kput(c);
		}
		int ret = read(keyboard, &c, 1);
		assert(ret == 1);
		if (c == SCANCODE_ESC) {
			kprintf("%c\n", c);
			break;
		}
		if (c == '0') {
			dump_help(list);
			continue;
		}
		eaten = 0;
		for (size_t i = 0; cmds[i].task_entry && cmds[i].name; ++i) {
			if (c == cmds[i].shortcut_spawn) {
				eaten = 1;
				const char *name = NULL;
				if (cmds[i].task_entry == enter_cmdlist)
					name = ((struct cmd_list *)cmds[i].ctx)->name;
				else
					name = cmds[i].name;
				kprintf("%s\n", name);
				tid_t ret = spawn_or_run(&arena, &tasks, &tasks_free, &cmds[i]);
				if (ret < 0)
					break;
				if (cmds[i].max_tids == 1)
					wait_tid(ret);
			}
			if (c == cmds[i].shortcut_kill) {
				eaten = 1;
				kprintf("\nkill(%s)", cmds[i].name);
				int ret = kill_or_nah(&tasks_free, &cmds[i]);
				if (ret < 0)
					kprintf(" -> Failed (%i)\n", ret);
				else
					kprintf(" -> %i\n", ret);
				break;
			}
		}
	}
	kprintf("exiting %s\n", name);
	dev_char_close(keyboard);
	kfree(console_buf);
	return 0;
}

int readme(void *ctx) {
	(void)ctx;
	kprintf(
	    "\tThis is my attempt at building an x86 kernel to learn about\n"
	    "\toperating systems and programming hardware. I'm targeting the\n"
	    "\tPC you see here, it's a generic 80486DX2-66 system with 32MB\n"
	    "\tof RAM. Feel free to press keys as indicated. You'll likely\n"
	    "\tcrash the system, which is fine. Just hit the middle button on\n"
	    "\tthe system unit (the one with the key cap missing) to reset the\n"
	    "\tsystem. Features currently (mostly) working:\n"
	    "\t\t- custom, from-scratch bootloader (in 490 bytes)\n"
	    "\t\t- preemptive multi-tasking (software task switching)\n"
	    "\t\t- page frame allocator (using a freelist)\n"
	    "\t\t- very fragile virtual memory allocator\n"
	    "\t\t- 2 system calls (exit and sleep)\n"
	    "\t\t- drivers for serial + the minimal hardware needed to get up\n"
	    "\t\tand running on x86.\n"
	    "\tPing me (vkoskiv) if you want to know more :]\n"
	);
	return 0;
}

int lightmode(void *ctx) {
	toggle_dark_mode();
	return 0;
}

static void hexdump(uint8_t *data, size_t bytes) {
	int tmp = 0;
	for (size_t i = 0; i < bytes; ++i) {
		kprintf("%0h", data[i]);
		++tmp;
		if (tmp == 27)
			tmp = 0;
		else
			kput(' ');
	}
	kput('\n');
}

int hash_all_sectors(void *ctx) {
	(void)ctx;
	struct dev_block *fd0 = dev_block_open("fd0");
	if (!fd0) {
		kprintf("no block device fd0 :(\n");
		return -1;
	}
	int bs = fd0->block_size(&fd0->base);
	int blocks = fd0->block_count(&fd0->base);
	kprintf("bs %i, name: %s, %i blocks to hash\n", bs, fd0->base.name, blocks);

	v_hash prev = 0;
	uint8_t *buf = kmalloc(bs);
	for (int sec = 0; sec < blocks; ++sec) {
		int ret = fd0->block_read(&fd0->base, sec, (char *)buf);
		if (ret) {
			kprintf("block_read(%i) returned %i\n", sec, ret);
			kfree(buf);
			return ret;
		}
		v_hash hash = v_hash_init();
		v_hash_bytes(&hash, buf, bs);
		kprintf("%h (%i)%s", hash, sec, hash == prev ? "\r" : "\n");
		prev = hash;
	}
	kput('\n');

	kfree(buf);
	return 0;
}

int dump_sector(void *ctx) {
	(void)ctx;

	struct dev_block *fd0 = dev_block_open("fd0");
	if (!fd0) {
		kprintf("no block device fd0 :(\n");
		return -1;
	}
	int bs = fd0->block_size(&fd0->base);
	kprintf("bs %i, name: %s\n", bs, fd0->base.name);

	uint8_t *buf = kmalloc(bs);

	int ret = fd0->block_read(&fd0->base, 0, (char *)buf);
	if (ret) {
		kprintf("block_read returned %i\n", ret);
		kfree(buf);
		return ret;
	}
	hexdump(buf, bs);
	kfree(buf);
	return 0;
}

int kill_tests(void *ctx) {
	(void)ctx;
	kill_sig++;
	spot_idx = 0;
	return 0;
}

extern struct cmd_list fd_debug;
extern struct cmd_list ser_debug;
extern struct cmd_list sync_debug;
extern struct cmd_list slab_debug;
static struct cmd_list console = {
	.name = "console",
	.cmds = {
		{ {}, 0,  1, NULL,      TASK(readme),         "Show README",                  'r',  0  },
		{ {}, 0,  1, NULL,      TASK(dump_stage0_pd), "dump pd",                      '1',  0  },
		{ {}, 0,  1, NULL,      TASK(dump_mem_stats), "show memory stats",            '2',  0  },
		// { {}, 0,  0, NULL,      TASK(toggle_dark_mode), "toggle dark mode",           '3',  0  },
		{ {}, 0, -1, NULL,      TASK(stack_overflow_gentle), "Blow the stack gently", '5', 't' },
		{ {}, 0, -1, NULL,      TASK(stack_overflow_hard), "Blow the stack hard",     '6', 'y' },
		{ {}, 0, -1, &spot_idx, TASK(kmalloc_stress), "stress kmalloc()",             '7', 'u' },
		{ {}, 0, -1, &spot_idx, TASK(vga_flasher), "VGA flasher task",                '8', 'i' },
		{ {}, 0,  1, NULL,      TASK(dump_tasks), "List running tasks",               '9',  0  },
		{ {}, 0,  1, NULL,      TASK(kill_tests), "kill running test tasks",          'x',  0  },
		{ {}, 0,  1, NULL,      TASK(dump_irq_counts), "dump IRQ counts",             'q',  0  },
		{ {}, 0, -1, NULL,      TASK(lightmode),  "darkmode",                          'l', 0 },
		{ {}, 0,  1, NULL,      TASK(dump_sector),  "dump boot sector",               ',', 0 },
		{ {}, 0,  1, NULL,      TASK(hash_all_sectors),  "fnv hash all sectors",      '.', 0 },
		{ {}, 0,  1, &kpftest,  TASK(enter_cmdlist), "enter kprintf test",            'k',  0  },
		{ {}, 0,  1, &fd_debug, TASK(enter_cmdlist), "enter floppy debug",            'p',  0  },
		{ {}, 0,  1, &ser_debug, TASK(enter_cmdlist), "enter serial debug",           'o',  0  },
		{ {}, 0,  1, &sync_debug, TASK(enter_cmdlist), "enter sync debug",            's',  0  },
		{ {}, 0,  1, &user_tests,TASK(enter_cmdlist), "usermode tests",               'd',  0  },
		{ {}, 0,  1, &slab_debug ,TASK(enter_cmdlist), "slab allocator debug",        'a',  0  },
		{ 0 },
	}
};
int console_task(void *ctx) {
	(void)ctx;
	enter_cmdlist(&console);
	return 0;
}
