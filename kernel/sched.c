#include <kprintf.h>
#include <utils.h>
#include <sched.h>
#include <mm/vma.h>
#include <kmalloc.h>
#include <timer.h>
#include <assert.h>
#include <x86.h>
#include <debug.h>

#if defined(DEBUG_SCHED)
#include <serial_debug.h>
#endif

// Just to satisfy vmalloc() until scheduler is set up.
struct task fake = {
	.id = -1,
	.cli_depth = 0,
	.name = "stage0_earlyinit",
};

struct task *current = &fake;

static tid_t last_tid = 0;

V_ILIST(runqueue);
struct semaphore reaper_call = { 0 };

void dump_kill_reason(struct task *t) {
	if (t->k_esp < (uint32_t)t->redzone_top) {
		kprintf("*%s(%i): stack overflow (esp %h %i bytes into redzone)\n",
		    t->name, t->id, t->k_esp, (uint8_t *)t->redzone_top - t->k_esp);
	} else if (DEBUG_TASK_START_STOP) {
		kprintf("-%s(%i): %i\n", t->name, t->id, t->ret);
	}
}

static int reaper(void *ctx) {
	(void)ctx;
	for (;;) {
		sem_pend(&reaper_call);
		v_ilist *pos, *temp;
		cli_push();
		v_ilist_for_each_safe(pos, temp, &runqueue) {
			struct task *t = v_ilist_get(pos, struct task, linkage);
			if (t->state != ts_stopping)
				continue;
			assert(t != current);
			dump_kill_reason(t);
			v_ilist_remove(&t->linkage);
			// wake waiters, if any
			v_ilist *wpos, *wtemp;
			v_ilist_for_each_safe(wpos, wtemp, &t->waiters) {
				struct task *w = v_ilist_get(wpos, struct task, waiting_on);
				// kprintf("reaper: resuming %s[%i]\n", w->name, w->id);
				v_ilist_remove(&w->waiting_on);
				w->state = ts_runnable;
			}
			if (t->stack_user)
				kfree(t->stack_user);
			kfree(t->stack_kernel);
			kfree(t);
		}
		cli_pop();
	}
	return 0;
}

static int do_idle(void *ctx) {
	(void)ctx;
	for (;;)
		asm("hlt");
	assert(NORETURN);
	return 0;
}

struct task *idle_task = NULL;

void sched_init(void) {
	sem_init(&reaper_call, 0);
	tid_t ret = task_create(do_idle, NULL, "kidle", 0);
	assert(ret >= 0);
	idle_task = v_ilist_get_first(&runqueue, struct task, linkage);
	assert(idle_task);
	assert(idle_task->id == 0);
	assert(!current->cli_depth);
	idle_task->state = ts_wait;

	// Other kernel background tasks here.
	task_create(reaper, NULL, "kreaper", 0);
}

#define TASK_STACK_SIZE (TASK_STACK_PAGES * PAGE_SIZE)

static char statechar(enum task_state s) {
	switch (s) {
	case ts_dead: return     'D';
	case ts_runnable: return 'R';
	case ts_wait:
	case ts_wait_task:
	case ts_wait_semaphore: return  'W';
	case ts_sleeping: return 'S';
	case ts_stopping: return 'E';
	}
}

static void dump_tasks(struct task *prev, struct task *next) {
	v_ilist *pos;
	v_ilist_for_each(pos, &runqueue) {
		struct task *t = v_ilist_get(pos, struct task, linkage);
		char transition = ' ';
		if (t == prev)
			transition = '<';
		else if (t == next)
			transition = '>';
		kprintf("%c[%i|%u|%c]%s", transition, t->ticks, t->id, statechar(t->state), t->name);
		if (t->state == ts_wait_task) {
			struct task *waitee = v_ilist_get_first(&t->waiting_on, struct task, waiters);
			kprintf(" <- %s(%u)\n", waitee->name, waitee->id);
		} else if (t->state == ts_wait_semaphore) {
			struct semaphore *s = v_ilist_get_first(&t->waiting_on, struct semaphore, waiters);
			kprintf(" <- semaphore %h\n", s);
		} else {
			kput('\n');
		}
	}
	kput('\n');
}

// static void dump_task(struct task *t) {
// 	uint8_t *stack = t->stack_kernel + TASK_STACK_SIZE;
// 	uint8_t *redzone = t->stack_kernel;
// 	kprintf("[%i] %s(%h)\n\tstack   %h-%h\n\tredzone %h-%h\n",
// 		t->id, t->name, t->entry,
// 		stack, stack + TASK_STACK_SIZE,
// 		redzone, redzone + TASK_STACK_SIZE);
// }

void dump_running_tasks(void) {
	cli_push();
	dump_tasks(NULL, NULL);
	cli_pop();
}

void task_entry_point(void) {
	struct task *t = current;
	assert(t->entry);
	t->ret = t->entry(t->ctx);
	cli();
	t->state = ts_stopping;
	sem_post(&reaper_call);
	sched();
	assert(NORETURN);
}

/*
	Note: eoi(0) is normally called from do_irq() after
	it calls do_timer(), which calls sched(). But when a task is
	scheduled for the first time, the first return from sched() comes
	via task_init here, not irq0_handler, so we also need to call eoi(0)
	here as well. 32 is IRQ0_OFFSET
*/
void task_init();
asm(
".globl task_init\n"
"task_init:"
"	pop ebx;" // stack->data_selector
"	mov ds, bx;"
"	mov es, bx;"
"	mov fs, bx;"
"	mov gs, bx;"
"	xor eax, eax;"
"	xor ebx, ebx;"
"	xor ecx, ecx;"
"	xor edx, edx;"
"	xor esi, esi;"
"	xor edi, edi;"
"	xor ebp, ebp;"
"	push 32;"
"	call pic_eoi;"
"	add esp, 4;"
"	iret;"
);

struct new_task_stack {
	uint32_t ebp, edi, esi, ebx;
	uint32_t initial_return_addr;
	uint32_t data_selector;
	// popped by iret
	void (*eip)(void);
	uint32_t cs, eflags, usermode_esp, usermode_ss;
};

tid_t task_create(int (*func)(void *), void *ctx, const char *name, int user_task) {
	struct task *new = kmalloc(sizeof(*new));
	if (!new)
		return -1;
	new->name = name;
	new->waiters = V_ILIST_INIT(new->waiters);
	new->waiting_on = V_ILIST_INIT(new->waiting_on);
	new->id = last_tid++;
	new->state = ts_runnable;
	new->ticks = new->priority = 20;

	new->stack_kernel = kmalloc(2 * TASK_STACK_SIZE);
	assert(new->stack_kernel);
	// Can't catch page fault on stack overflow, so work around that by
	// allocating a redzone that is checked every time the task comes off the
	// CPU, and kill the task if its stack pointer dips into the redzone.
	// It's still theoretically possible that a task could overflow its
	// redzone in a single timeslice. FIXME: Figure out a solution for that maybe.
	new->redzone_top = (uint8_t *)new->stack_kernel + TASK_STACK_SIZE;

	uint32_t cs = user_task ? (GDT_USER_CODE | 3) : GDT_KERNEL_CODE;
	uint32_t ds = user_task ? (GDT_USER_DATA | 3) : GDT_KERNEL_DATA;

	// Construct a bootstrap stack that this new task will pop when it
	// first starts executing from switch_to()
	uint8_t *k_sptr = (uint8_t *)new->stack_kernel + (2 * TASK_STACK_SIZE);
	k_sptr -= sizeof(struct new_task_stack);
	struct new_task_stack *stack = (void *)k_sptr;
	stack->ebp = stack->edi = stack->esi = stack->ebx = 0;
	stack->initial_return_addr = (uint32_t)task_init;
	stack->data_selector = ds;
	stack->eip = task_entry_point; // Returns here after task_init iret
	stack->cs = cs;
	stack->eflags = 0x200; // Enable interrupts

	new->k_esp = (uint32_t)k_sptr;
	new->entry = func;
	new->ctx = ctx;

	if (user_task) {
		new->stack_user = kmalloc(2 * TASK_STACK_SIZE);
		assert(new->stack_user);
		// FIXME: kernel & user stacks are same size, maybe reconsider
		// FIXME: new_task_user_stack struct?
		uint8_t *u_sptr = (uint8_t *)new->stack_user + (2 * TASK_STACK_SIZE);
		// push id for ctx
		u_sptr -= 4;
		*(uint32_t *)u_sptr = new->id;
		// And a NULL return address
		u_sptr -= 4;
		*(uint32_t *)u_sptr = 0;
		stack->usermode_esp = (uint32_t)u_sptr;
		stack->usermode_ss = ds;

		// FIXME: Hack - I don't have the ability to map a userspace virtual
		// address space yet, so just mark the usermode task stack as well as
		// the page 'func' lives in as user pages to get the ball rolling.
		// Obviously replace this with proper logic to set up & tear down new
		// address spaces + cr3 swapping.
		assert(!mprotect((void *)PAGE_ROUND_DN(func), PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_USR));
		assert(!mprotect((void *)new->stack_user, 2 * TASK_STACK_SIZE, PROT_WRITE | PROT_USR));

		stack->eip = (void *)new->entry;
		new->u_esp = (uint32_t)u_sptr;
	}

	cli_push();
	v_ilist_prepend(&new->linkage, &runqueue);
	cli_pop();
#if DEBUG_TASK_START_STOP == 1
	kprintf("+%s(%i)\n", new->name, new->id);
#endif
	return new->id;
}

static struct task *find_task(tid_t tid) {
	v_ilist *pos;
	v_ilist_for_each(pos, &runqueue) {
		struct task *t = v_ilist_get(pos, struct task, linkage);
		if (t->id == tid)
			return t;
	}
	return NULL;
}

int task_kill(tid_t id) {
	if (id < 1)
		return -1;
	cli_push();
	struct task *to_kill = find_task(id);
	if (!to_kill) {
		cli_pop();
		return -1;
	}
	to_kill->state = ts_stopping;
	sem_post(&reaper_call);
	cli_pop();
	return to_kill->id;
}

int wait_tid(tid_t task_id) {
	if (task_id < 1)
		return -1;
	cli_push();
	struct task *waitee = find_task(task_id);
	if (!waitee)
		return -1;

	// FIXME: use semaphore for this
	current->state = ts_wait_task;
	v_ilist_prepend(&current->waiting_on, &waitee->waiters);

	sched();
	cli_pop();
	return 0;
}

void switch_to(struct task *prev, struct task *next);
asm(
".globl switch_to\n"
"switch_to:"
"	mov eax, [esp+4];" // *prev
"	mov edx, [esp+8];" // *next
"	push ebx;"
"	push esi;"
"	push edi;"
"	push ebp;"
"	mov [eax+4], esp;" // prev->k_esp <- esp
"	mov esp, [edx+4];" // esp <- next->k_esp
"	pop ebp;"
"	pop edi;"
"	pop esi;"
"	pop ebx;"
"	ret;"
);

void switch_to_initial(struct task *prev, struct task *next);
asm(
".globl switch_to_initial\n"
"switch_to_initial:"
"	mov eax, [esp+4];" // *prev
"	mov edx, [esp+8];" // *next
"	push ebx;"
"	push esi;"
"	push edi;"
"	push ebp;" // <---- Just missing prev->k_esp store
"	mov esp, [edx+4];" // esp <- next->k_esp
"	pop ebp;"
"	pop edi;"
"	pop esi;"
"	pop ebx;"
"	ret;" // <-- to task_init:
);

static inline void update_task_state(struct task *t, uint32_t ms) {
	if (t->state == ts_runnable && t->k_esp < (uintptr_t)t->redzone_top) {
		t->state = ts_stopping;
		sem_post(&reaper_call);
	}
	if (t->state == ts_sleeping && ms >= t->sleep_till)
		t->state = ts_runnable;
	
}

static inline struct task *find_next_runnable(v_ilist *head) {
	uint32_t ms = system_uptime_ms;
	for (;;) {
		struct task *next = NULL;
		int32_t max_ticks = -1;
		v_ilist_for_each(head, &runqueue) {
			struct task *t = v_ilist_get(head, struct task, linkage);
			// if (t == idle_task)
			// 	continue;
			update_task_state(t, ms);
			if (t->state == ts_runnable && t->ticks > max_ticks) {
				next = t;
				max_ticks = next->ticks;
			}
		}
		if (max_ticks)
			return next;
		v_ilist_for_each(head, &runqueue) {
			struct task *t = v_ilist_get(head, struct task, linkage);
			t->ticks = (t->ticks >> 1) + t->priority;
		}
	}
}

#if DEBUG_SCHED == 1
static uint32_t beats = 0;
#endif

void sched(void) {
#if DEBUG_SCHED == 1
	if (beats++ % 50 == 0)
		serial_out_byte('0' + current->id);
#endif
	struct task *next = find_next_runnable(&current->linkage);
	if (!next)
		next = idle_task;
	if (next == current)
		return;
	struct task *prev = current;
	current = next;
	// CPU loads g_tss.esp0 when moving user -> kernel on interrupt.
	// This tells the CPU to establish an empty kernel stack when a
	// task enters kernel mode. If a task gets preempted while in kernel mode,
	// it will get its esp from k_esp when resuming, since there is no change
	// in privilege level.
	// ss0 is already set to GDT_KERNEL_DATA in gdt_init().
	g_tss.esp0 = (uint32_t)next->stack_kernel + (2 * TASK_STACK_SIZE);
	if (DEBUG_TASK_SWITCH)
		dump_tasks(prev, next);
	switch_to(prev, next);
}

// TODO: Would be nice to find a better solution than having to
// mostly duplicate sched() and switch_to() just for the first
// task switch from stage1. The initial task switch is special
// because it does not overwrite prev->k_esp
void sched_initial(void) {
#if DEBUG_SCHED == 1
	if (beats++ % 50 == 0)
		serial_out_byte('0' + current->id);
#endif
	struct task *next = find_next_runnable(&current->linkage);
	assert(next);
	assert(next != current);
	struct task *prev = current;
	current = next;
	if (DEBUG_TASK_SWITCH)
		dump_tasks(prev, next);
	switch_to_initial(prev, next);
}
