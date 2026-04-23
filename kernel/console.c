#include <console.h>
#include <mm/vma.h>
#include <mm/pfa.h>
#include <kmalloc.h>
#include <sched.h>
#include <timer.h>
#include <assert.h>
#include <syscalls.h>
#include <kprintf.h>
#include <fs/dev_block.h>
#include <x86.h>
#include <mm/purge.h>
#include <types.h>
#include <fs/vfs.h>
#include <errno.h>

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

#define N_SPAWN 10
static int n_stress_tasks = 0;
static int s_pfa_stress_initial_free = -1;
#define PFA_STRESS_RESERVATION 256

static int pfa_stress_task(void *ctx) {
	uint32_t spot_idx = (uint32_t)ctx;
	uint8_t x = DX - (spot_idx / DY);
	uint8_t y = (spot_idx % DY);
	char c = 0x21;
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
		uint16_t max_rand = (s_pfa_stress_initial_free - PFA_STRESS_RESERVATION) / n_stress_tasks;
		max_rand = min(max_rand, 256);
		rand_count = (((system_uptime_ms >> 8) | system_uptime_ms) & 0xFFFF) % max_rand;
		rand_sleep = ((rand_count % 16)/2) + 1;
		flip = (rand_count | rand_sleep) > 128 ? 1 : 0;
		// kprintf("sleep %u %u, count: %u, flip: %u\n", rand_sleep, rand_sleep, rand_count, flip);

		for (size_t i = 0; i < rand_count; ++i)
			while (!(allocs[i] = pf_alloc()));

		sleep(rand_sleep*2);
		// sleep(100);
		// for (size_t i = 0; i < rand_count; ++i)
		// 	memset(allocs[i], 0x41, PAGE_SIZE);

		vga_hackbuf[y * VGA_WIDTH + x] = ((uint16_t)c++ & 0xff) | (uint16_t)color<<8;
		if (c >= 0x7E)
			c = 0x21;
		if (flip) {
			for (uint16_t i = rand_count; i --> 0;)
				pf_free(allocs[i]);
		} else {
			for (size_t i = 0; i < rand_count; ++i)
				pf_free(allocs[i]);
		}
		sleep(rand_sleep*2);

		if (kill_sig != orig_kill)
			break;
	}
	vga_hackbuf[y * VGA_WIDTH + x] = ((uint16_t)' ' & 0xff) | (uint16_t)(15|0<<4)<<8;
	return 0;
}

static int vmalloc_stress_task(void *ctx) {
	uint32_t *idx = ctx;
	uint32_t spot_idx = *idx;
	(*idx)++;
	uint8_t x = DX - (spot_idx / DY);
	uint8_t y = (spot_idx % DY);
	char c = 0x21;
	s_fgcolor++;
	if (s_fgcolor == 0x7)
		s_fgcolor++; // Skip grey-on-grey
	uint8_t color = (uint8_t)(s_fgcolor | s_bgcolor << 4);
	if (s_fgcolor > 15)
		s_fgcolor = 1;

	uint8_t rand_sleep = 0;
	uint16_t rand_count = 0;

	uint32_t orig_kill = kill_sig;

	while (1) {
		uint16_t max_rand = 256;
		rand_count = (((system_uptime_ms >> 8) | system_uptime_ms) & 0xFFFF) % max_rand;
		rand_sleep = ((rand_count % 16)/2) + 1;

		void *vmbuf = vmalloc(rand_count * PAGE_SIZE);
		if (!vmbuf) {
			kprintf("vmalloc_stress(%i) failed to allocate %u pages, exiting.\n", current->id, rand_count);
			break;
		}

		sleep(rand_sleep*2);

		vga_hackbuf[y * VGA_WIDTH + x] = ((uint16_t)c++ & 0xff) | (uint16_t)color<<8;
		if (c >= 0x7E)
			c = 0x21;
		vmfree(vmbuf);
		sleep(rand_sleep*2);

		if (kill_sig != orig_kill)
			break;
	}
	vga_hackbuf[y * VGA_WIDTH + x] = ((uint16_t)' ' & 0xff) | (uint16_t)(15|0<<4)<<8;
	return 0;
}

static int dump_free_pages(void *ctx);
static int pfa_stress(void *ctx) {
	uint32_t *spot_idx = ctx;
	(void)ctx;
	if (s_pfa_stress_initial_free < 0)
		s_pfa_stress_initial_free = pfa_count_free_pages();
	kprintf("starting %i stress tasks,\nallocating up to %i pages,\ntotal tasks: %i\n", N_SPAWN, s_pfa_stress_initial_free - PFA_STRESS_RESERVATION, n_stress_tasks);

	n_stress_tasks += N_SPAWN;
	// cli_push();
	for (int i = 0; i < N_SPAWN; ++i) {
		tid_t ret = task_create(pfa_stress_task, (void *)*spot_idx, "pfa_stress_task", 0);
		if (ret < 0) {
			kprintf("Failed to spawn task %i (%i)\n", i, ret);
			kill_sig++;
			break;
		}
		*spot_idx += 1;
	}
	// cli_pop();
	kput('\n');
	sleep(10);
	dump_free_pages(NULL);
	return 0;
}

static int vga_flasher(void *ctx) {
	// FIXME: sys$nice
	current->priority = 5;
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
		sleep(16);
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

static int dump_mem_stats(void *ctx) {
	(void)ctx;
	dump_vm_ranges("vm_regions");
	uint8_t buf[256];
	v_ma temp = v_ma_from_arr(buf);
	dump_phys_mem_stats(temp, 1);
	return 0;
}

static int dump_free_pages(void *ctx) {
	(void)ctx;
	kprintf("free pages: %u\n", pfa_count_free_pages());
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
	(void)ctx;
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

int drv = 0;
const char *drvs[2] = { "/dev/fd0", "/dev/fd1" };

int swapdrv(void *ctx) {
	drv = !drv;
	kprintf("using %s\n", drvs[drv]);
	return 0;
}

int hash_all_sectors(void *ctx) {
	(void)ctx;
	struct vfs_file *drive = vfs_open_file(drvs[drv]);
	if (!drive) {
		kprintf("no block device %s\n", drvs[drv]);
		return -1;
	}
	struct vfs_stat sb;
	int ret = vfs_stat(drive, &sb);
	if (ret)
		return ret;
	int blocks = sb.size / sb.block_size;
	kprintf("bs %i, name: %s, %i blocks to hash\n", sb.block_size, drvs[drv], blocks);

	v_hash prev = 0;
	uint8_t *buf = kmalloc(sb.block_size);
	for (int sec = 0; sec < blocks; ++sec) {
		ret = vfs_read_at(drive, buf, sb.block_size, sec * sb.block_size);
		if (ret) {
			kprintf("vfs_read_at(%i) returned %i (%s)\n", sec, ret, strerror(ret));
			kfree(buf);
			vfs_close(drive);
			return ret;
		}
		v_hash hash = v_hash_init();
		v_hash_bytes(&hash, buf, sb.block_size);
		kprintf("%h (%i)%s", hash, sec, hash == prev ? "\r" : "\n");
		prev = hash;
	}
	kput('\n');

	vfs_close(drive);
	kfree(buf);
	return 0;
}

int dump_sector(void *ctx) {
	(void)ctx;

	struct vfs_file *drive = vfs_open_file(drvs[drv]);
	if (!drive) {
		kprintf("no block device %s\n", drvs[drv]);
		return -1;
	}
	struct vfs_stat sb;
	int ret = vfs_stat(drive, &sb);
	if (ret)
		return ret;
	kprintf("bs %i, name: %s\n", sb.block_size, drvs[drv]);

	uint8_t *buf = kmalloc(sb.block_size);

	ret = vfs_read_at(drive, buf, sb.block_size, 0);
	if (ret) {
		kprintf("vfs_read_at(0) returned %i (%s)\n", ret, strerror(ret));
		kfree(buf);
		vfs_close(drive);
		return ret;
	}
	hexdump(buf, sb.block_size);
	kfree(buf);
	vfs_close(drive);
	return 0;
}

#define PRECISION 100
static uint32_t benchmark(int iters, void (*func)(void *), void *ctx) {
	uint32_t *intervals = kmalloc(iters * sizeof(*intervals));
	uint32_t sum = 0;
	for (int i = 0; i < iters; ++i) {
		uint32_t before = system_uptime_ms;
		func(ctx);
		uint32_t after = system_uptime_ms;
		intervals[i] = after - before;
		sum += intervals[i] * PRECISION;
	}
	kfree(intervals);
	return sum / (iters * PRECISION);
}

static size_t old_strlen(const char *str) {
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}

void old(void *ctx) {
	old_strlen(ctx);
}

void new(void *ctx) {
	strlen(ctx);
}

struct purgeable *strlen_test_buf = NULL;

int test_strlen(void *ctx) {
	(void)ctx;

	const ssize_t bufsize = 1048576;
	if (!strlen_test_buf) {
		kprintf("preparing buffer\n");
		strlen_test_buf = purgeable_alloc(bufsize, NULL, NULL);
		char *textbuf = purgeable_get(strlen_test_buf, NULL);
		for (int i = 0; i < bufsize; ++i)
			textbuf[i] = 'A';
		textbuf[bufsize - 1] = 0;
		purgeable_put(textbuf);
	}

	int iters = 100;
	int purged = 0;
	char *textbuf = purgeable_get(strlen_test_buf, &purged);
	if (purged) {
		for (int i = 0; i < bufsize; ++i)
			textbuf[i] = 'A';
		textbuf[bufsize - 1] = 0;
	}
	kprintf("running %i iterations of old strlen()\n", iters);
	uint32_t avg = benchmark(iters, old, textbuf);
	kprintf("strlen() avg: ~%ums\n", avg);

	kprintf("running %i iterations of new strlen()\n", iters);
	avg = benchmark(iters, new, textbuf);
	kprintf("strlen_fast() avg: ~%ums\n", avg);
	purgeable_put(textbuf);

	kprintf("done, bye!\n");

	return 0;
}

int kill_tests(void *ctx) {
	(void)ctx;
	kill_sig++;
	spot_idx = 0;
	sleep(100);
	n_stress_tasks = 0;
	return 0;
}

static int run_purge(void *ctx) {
	(void)ctx;
	size_t purged = purgeable_purge(0);
	kprintf("purged %u bytes\n", purged);
	return 0;
}

// Prints 'load &file' as 'load [f]ile'
static void dump_name(const char *name, int plain) {
	while (*name) {
		if (*name == '&' && *(name + 1) && *(name + 1) != ' ') {
			if (*(name + 1) == '\x1B')
				kprintf("[ESC]");
			else
				kprintf(plain ? "%c" : "[%c]", *(name + 1));
			name += 2;
		} else {
			kput(*name++);
		}
	}
}

static char get_shortcut(const char *name) {
	char c = 0;
	while (*name) {
		if (*name == '&' && *(name + 1) && *(name + 1) != ' ') {
			c = *(name + 1);
			break;
		}
		name++;
	}
	// TODO: tolower() & friends.
	if (c >= 'A' && c <= 'Z')
		c += 32;
	return c;
}

int cmd_list(void *ctx) {
	const struct command *cmd = ctx;
	const struct command *cmds = cmd->cmds;
	for (size_t i = 0; cmds[i].fn && cmds[i].name; ++i) {
		char shortcut = get_shortcut(cmds[i].name);
		if (!shortcut)
			continue;
		kput('\t');
		dump_name(cmds[i].name, 0);
		if (cmds[i].fn == cmd_enter_menu)
			kprintf(" >");
		kput('\n');
	}
	return 0;
}

int cmd_enter_menu(void *ctx) {
	struct command *this = ctx;
	// FIXME: add setup code to task_create() that opens files for every task
	// 0, stdin -> keyboard
	// 1, stdout -> console
	// 2, stderr -> console
	// Then add machinery so read() takes a fd, which then indexes into
	// to a struct device array in current->files or something
	struct vfs_file *stdin = vfs_open_file("/dev/kbd");
	if (!stdin) {
		kprintf("Couldn't open /dev/kbd\n");
		return 1;
	}
	char c = 0;
	int ret = 0;
	cmd_list(this);
	for (;;) {
nextcmd:
		dump_name(this->name, 1);
		kprintf("> ");
next:
		ret = vfs_read(stdin, &c, 1);
		assert(ret == 1);
		const struct command *cmds = this->cmds;
		for (size_t i = 0; cmds[i].fn && cmds[i].name; ++i) {
			char shortcut = get_shortcut(cmds[i].name);
			if (!shortcut)
				continue;
			if (c == shortcut) {
				void *arg = cmds[i].ctx;
				if (!arg)
					arg = cmds[i].fn == cmd_enter_menu ? (void *)&cmds[i] : this;
				if (cmds[i].fn == cmd_exit)
					kput(c);
				else
					dump_name(cmds[i].name, 0);
				kput('\n');
				ret = cmds[i].fn(arg);
				if (ret == -1)
					goto exit;
				if (ret)
					kprintf("exited with %i\n", ret);
				goto nextcmd;
			}
		}
		kput(c);
		if (c == '\n')
			goto nextcmd;
		goto next;
	}
exit:
	return vfs_close(stdin);
}

int cmd_exit(void *ctx) {
	(void)ctx;
	return -1;
}

int cmd_run_task(void *ctx) {
	struct cmd_arg *arg = ctx;
	tid_t tid = task_create(arg->fn, arg->ctx, arg->name, 0);
	if (tid < 0)
		return -tid;
	return wait_tid(tid);
}

int cmd_spawn_job(void *ctx) {
	struct cmd_arg *arg = ctx;
	tid_t tid = task_create(arg->fn, arg->ctx, arg->name, 0);
	if (tid < 0)
		return -tid;
	// FIXME: since task_create() marks it as runnable right away, it's possible that
	// we are preempted before printing this, which would look a bit goofy.
	kprintf("spawn('%s') -> %i\n", arg->name, tid);
	return 0;
}

static void check_duplicates(const struct command *cmd) {
	const struct command *cmds = cmd->cmds;
	for (size_t i = 0; cmds[i].fn && cmds[i].name; ++i) {
		char shortcut = get_shortcut(cmds[i].name);
		for (size_t j = 0; cmds[j].fn && cmds[j].name; ++j) {
			if (j == i)
				continue;
			if (get_shortcut(cmds[j].name) == shortcut)
				panic("\nIn menu '%s': multiple menu items with same key:\n\t[%u]'%s'\n\t[%u]'%s'",
				      cmd->name, i, cmds[i].name, j, cmds[j].name);
		}
	}
	for (size_t i = 0; cmds[i].fn && cmds[i].name; ++i)
		if (cmds[i].fn == cmd_enter_menu)
			check_duplicates(&cmds[i]);
}

extern const struct command slab_debug;
extern const struct command sync_debug;
extern const struct command fd_debug;
extern const struct command ser_debug;

int vfs_debug_shell(void *ctx);
int vfs_test(void *ctx);

static struct command console = MENU("console",
    FUNC("Show &README", readme, NULL),
    FUNC("&zap purgeable memory", run_purge, NULL),
    FUNC("toggle &light mode", lightmode, NULL),
    FUNC("&kill running test tasks", kill_tests, NULL),
    SUBMENU("&blkdev tests",
	    CMD("s&wap drive", swapdrv, NULL),
	    CMD("dump &boot sector", dump_sector, NULL),
	    CMD("fnv &hash all sectors", hash_all_sectors, NULL),
    ),
    SUBMENU("&info",
		FUNC("Show &page directory", dump_stage0_pd, NULL),
		FUNC("Show &memory", dump_mem_stats, NULL),
		FUNC("Show &free pages", dump_free_pages, NULL),
		FUNC("dump IR&Q counts", dump_irq_counts, NULL),
		FUNC("dump &tasks", dump_tasks, NULL),
    ),
    SUBMENU("subsystem &debug",
		CMD("&Slab allocator", cmd_enter_menu, (void *)&slab_debug),
		CMD("s&ync", cmd_enter_menu, (void *)&sync_debug),
		CMD("&floppy", cmd_enter_menu, (void *)&fd_debug),
		CMD("s&erial", cmd_enter_menu, (void *)&ser_debug),
		CMD("str&len test", test_strlen, NULL),
		SUBMENU("&VFS",
			CMD("&Shell", vfs_debug_shell, NULL),
		    CMD("run &tests", vfs_test, NULL),
		),
    ),
    SUBMENU("&tests",
        SUBMENU("&usermode",
			JOB("Spawn user task calling sys$hello&1", u_hello1, NULL),
			JOB("Spawn user task calling sys$hello&2", u_hello1, NULL),
			JOB("Spawn user task calling sys$hello&3", u_hello1, NULL),
			JOB("Spawn user task calling sys$hello&4", u_hello1, NULL),
			JOB("Spawn user task calling sys$hello&5", u_hello1, NULL),
			JOB("Spawn user task calling sys$hello&6", u_hello1, NULL),
			JOB("Spawn user task calling sys$&sleep",  u_sleep,  NULL),
        ),
        SUBMENU("&stress",
            SUBMENU("&scheduler",
			    JOB("Spawn &VGA Flasher", vga_flasher, &spot_idx),
            ),
            SUBMENU("&memory",
				JOB("&pf_alloc()", pfa_stress, &spot_idx),
				JOB("&vmalloc()", vmalloc_stress_task, &spot_idx),
            ),
        ),
        SUBMENU("&crashes",
			JOB("blow stack &gently", stack_overflow_gentle, NULL),
			JOB("blow stack &hard", stack_overflow_hard, NULL),
        ),
    ),
);

int console_task(void *ctx) {
	(void)ctx;
	check_duplicates(&console);
	return cmd_enter_menu(&console);
}
