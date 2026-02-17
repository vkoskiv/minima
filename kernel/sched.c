#include "terminal.h"
#include "utils.h"
#include "irq_handlers.h"
#include "sched.h"
#include "mman.h"
#include "timer.h"

struct task *current = NULL;

static tid_t last_tid = 0;

V_ILIST(runqueue);
V_ILIST(tasks);

static void do_idle(void) {
	for (;;)
		asm("hlt");
}

struct task *idle_task = NULL;

void sched_init(void) {
	struct task *buf = kmalloc(MAX_TASKS * sizeof(struct task));
	for (size_t i = 0; i < MAX_TASKS; ++i)
		v_ilist_prepend(&buf[i].linkage, &tasks);
	task_create(do_idle, "idle_task");
	idle_task = v_ilist_get_first(&runqueue, struct task, linkage);
	v_ilist_remove(&idle_task->linkage);
	current = idle_task;
}

struct new_task_stack {
	uint32_t ebp, edi, esi, ebx;
	uint32_t initial_return_addr;
	// popped by iret
	uint32_t eip, cs, eflags; // usermode_esp, usermode_ss?
};

void task_init();
asm(
".globl task_init\n\t"
"task_init:\n\t"
"    iret\n\t"
);

static void dump_task(struct task *t) {
	uint8_t *stack = t->stack + PAGE_SIZE;
	uint8_t *redzone = t->stack;
	kprintf("[%i] %s(%h)\n\tstack   %h-%h\n\n",
		t->id, t->name, t->entry,
		stack, stack + PAGE_SIZE);
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

tid_t task_create(void (*func)(void), const char *name) {
	if (!v_ilist_count(&tasks))
		return -1;
	struct task *new = v_ilist_get_first(&tasks, struct task, linkage);
	v_ilist_remove(&new->linkage);
	new->name = name;
	new->id = last_tid++;
	if (!new->stack) // Reuse existing stack if possible
		new->stack = kmalloc(PAGE_SIZE);
	else
		kprintf("Reusing stack at %h for task %i\n", new->stack, new->id);

	// Construct a fake initial stack that sets up task
	uint8_t *sptr = (uint8_t *)new->stack + PAGE_SIZE;
	sptr -= sizeof(struct new_task_stack);
	struct new_task_stack *stack = (void *)sptr;
	stack->ebp = stack->edi = stack->esi = stack->ebx = 0;
	stack->initial_return_addr = (uint32_t)task_init;
	stack->eip = (uint32_t)func; // Returns here after task_init iret
	stack->cs = 0x08; // Kernel code
	stack->eflags = 0x200; // Enable interrupts

	new->esp = (uint32_t)sptr;
	new->entry = func;
	v_ilist_prepend(&new->linkage, &runqueue);
	return new->id;
}

void sched(void);
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
".globl switch_to\n\t"
"switch_to:\n\t"
	"movl 4(%esp), %eax\n\t" // *prev
	"movl 8(%esp), %edx\n\t" // *next
	"pushl %ebx\n\t"
	"pushl %esi\n\t"
	"pushl %edi\n\t"
	"pushl %ebp\n\t"
	"movl %esp, 4(%eax)\n\t"
	"movl 4(%edx), %esp\n\t"
	"popl %ebp\n\t"
	"popl %edi\n\t"
	"popl %esi\n\t"
	"popl %ebx\n\t"
	"ret\n\t"
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
	v_ilist_remove(&next->linkage);
	if (!next)
		next = idle_task;
	if (next == current)
		return;
	struct task *prev = current;
	v_ilist_prepend(&prev->linkage, &runqueue);
	current = next;
	switch_to(prev, next);
}
