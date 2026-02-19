#include "terminal.h"
#include "utils.h"
#include "irq_handlers.h"
#include "sched.h"
#include "mman.h"
#include "timer.h"
#include "assert.h"
#include "x86.h"
#include "debug.h"

struct task *current = NULL;

static tid_t last_tid = 0;

V_ILIST(runqueue);
V_ILIST(stop_queue);
V_ILIST(tasks);

static void dump_task(struct task *t);

#define REAPER_INTERVAL 100

void dump_kill_reason(struct task *t) {
	if (t->esp < (uint32_t)t->redzone_top) {
		kprintf(": stack overflow (esp %h %i bytes into redzone)\n",
		    t->esp, (uint8_t *)t->redzone_top - t->esp);
	} else {
		kput('\n');
	}
}

static int reaper(void *ctx) {
	(void)ctx;
	for (;;) {
		sleep(REAPER_INTERVAL);
		v_ilist *pos, *temp;
		cli();
		v_ilist_for_each_safe(pos, temp, &stop_queue) {
			struct task *t = v_ilist_get(pos, struct task, linkage);
			assert(t != current);
			if (!t->stopping) {
				kprintf("*%s(%i)", t->name, t->id);
				dump_kill_reason(t);
			}
			v_ilist_remove(&t->linkage);
			v_ilist_prepend(&t->linkage, &tasks);
		}
		sti();
	}
	return 0;
}

static int do_idle(void *ctx) {
	(void)ctx;
	for (;;)
		asm("hlt");
}

struct task *idle_task = NULL;

void sched_init(void) {
	struct task *buf = kmalloc(MAX_TASKS * sizeof(struct task));
	for (size_t i = 0; i < MAX_TASKS; ++i)
		v_ilist_prepend(&buf[i].linkage, &tasks);
	task_create(do_idle, NULL, "kidle");
	idle_task = v_ilist_get_first(&runqueue, struct task, linkage);
	v_ilist_remove(&idle_task->linkage);
	current = idle_task;

	// Other kernel background tasks here.
	task_create(reaper, NULL, "kreaper");
}

#define TASK_STACK_SIZE (TASK_STACK_PAGES * PAGE_SIZE)

static void dump_task(struct task *t) {
	uint8_t *stack = t->stack + TASK_STACK_SIZE;
	uint8_t *redzone = t->stack;
	kprintf("[%i] %s(%h)\n\tstack   %h-%h\n\tredzone %h-%h\n",
		t->id, t->name, t->entry,
		stack, stack + TASK_STACK_SIZE,
		redzone, redzone + TASK_STACK_SIZE);
}

void dump_running_tasks(void) {
	cli();
	kprintf("current:\n");
	dump_task(current);
	kprintf("runnable:\n");
	v_ilist *pos;
	v_ilist_for_each_rev(pos, &runqueue) {
		struct task *t = v_ilist_get(pos, struct task, linkage);
		dump_task(t);
	}
	sti();
}

void task_entry_point(void) {
	struct task *t = current;

	assert(t->entry);

#if DEBUG_TASK_START_STOP == 1
	kprintf("+%s(%i)\n", t->name, t->id);
#endif

	int ret = t->entry(t->ctx);

#if DEBUG_TASK_START_STOP == 1
	kprintf("-%s(%i): %i\n", t->name, t->id, ret);
#else
	(void)ret;
#endif
	
	cli();
	t->stopping = 1;
	sched();
	assert(NORETURN);
}

void task_init();
asm(
".globl task_init\n"
"task_init:"
"	iret;"
);

struct new_task_stack {
	uint32_t ebp, edi, esi, ebx;
	uint32_t initial_return_addr;
	// popped by iret
	void (*eip)(void);
	uint32_t cs, eflags; // usermode_esp, usermode_ss?
};

tid_t task_create(int (*func)(void *), void *ctx, const char *name) {
	if (!v_ilist_count(&tasks))
		return -1;
	struct task *new = v_ilist_get_first(&tasks, struct task, linkage);
	v_ilist_remove(&new->linkage);
	new->name = name;
	new->id = last_tid++;
	if (!new->stack) { // Reuse existing stack?
		new->stack = kmalloc(2 * TASK_STACK_SIZE);
		assert(new->stack);
		// Can't catch page fault on stack overflow, so work around that by
		// allocating a redzone that is checked every time the task comes off the
		// CPU, and kill the task if its stack pointer dips into the redzone.
		// It's still theoretically possible that a task could overflow its
		// redzone in a single timeslice. FIXME: Figure out a solution for that maybe.
		new->redzone_top = (uint8_t *)new->stack + TASK_STACK_SIZE;
	} else {
		kprintf("Reusing stack at %h for task %i\n", new->stack, new->id);
	}

	// Construct a bootstrap stack that this new task will pop when it
	// first starts executing from switch_to()
	uint8_t *sptr = (uint8_t *)new->stack + (2 * TASK_STACK_SIZE);
	sptr -= sizeof(struct new_task_stack);
	struct new_task_stack *stack = (void *)sptr;
	stack->ebp = stack->edi = stack->esi = stack->ebx = 0;
	stack->initial_return_addr = (uint32_t)task_init;
	stack->eip = task_entry_point; // Returns here after task_init iret
	stack->cs = 0x08; // Kernel code
	stack->eflags = 0x200; // Enable interrupts

	new->esp = (uint32_t)sptr;
	new->entry = func;
	new->ctx = ctx;
	v_ilist_prepend(&new->linkage, &runqueue);
	return new->id;
}

// FIXME: Mark a task/implement signals instead of doing all this
int task_kill(tid_t id) {
	if (id < 1)
		return -1;
	cli();
	v_ilist *pos, *temp;
	struct task *to_kill = NULL;
	v_ilist_for_each_safe(pos, temp, &runqueue) {
		struct task *t = v_ilist_get(pos, struct task, linkage);
		if (t->id == id) {
			v_ilist_remove(&t->linkage);
			to_kill = t;
			break;
		}
	}
	if (!to_kill)
		return -1;
	// Append because it already has a stack, so next task_create
	// will grab this.
	v_ilist_append(&to_kill->linkage, &tasks);
	sti();
	return to_kill->id;
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
"	mov [eax+4], esp;" // prev->esp <- esp
"	mov esp, [edx+4];" // esp <- next->esp
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
"	push ebp;" // <---- Just missing prev->esp store
"	mov esp, [edx+4];" // esp <- next->esp
"	pop ebp;"
"	pop edi;"
"	pop esi;"
"	pop ebx;"
"	ret;"
);

static inline struct task *find_next_runnable(void) {
	uint32_t ms = system_uptime_ms;
	v_ilist *pos;
	v_ilist_for_each(pos, &runqueue) {
		struct task *t = v_ilist_get(pos, struct task, linkage);
		if (t->sleep_till && t->sleep_till > ms)
			continue;
		return t;
	}
	return NULL;
}

void sched(void) {
	struct task *next = find_next_runnable();
	if (!next)
		next = idle_task;
	v_ilist_remove(&next->linkage);
	if (next == current)
		return;
	struct task *prev = current;
	if (prev->stopping || prev->esp < (uint32_t)prev->redzone_top)
		v_ilist_prepend(&prev->linkage, &stop_queue);
	else
		v_ilist_prepend(&prev->linkage, &runqueue);
	current = next;
	switch_to(prev, next);
}

// TODO: Would be nice to find a better solution than having to
// mostly duplicate sched() and switch_to() just for the first
// task switch from stage1
void sched_initial(void) {
	struct task *next = find_next_runnable();
	assert(next);
	v_ilist_remove(&next->linkage);
	assert(next != current);
	struct task *prev = current;
	v_ilist_prepend(&prev->linkage, &runqueue);
	current = next;
	switch_to_initial(prev, next);
}
