#include "terminal.h"
#include "utils.h"
#include "irq_handlers.h"
#include "sched.h"
#include "mman.h"
#include "timer.h"

struct task tasks[MAX_TASKS] = { 0 };
tid_t num_tasks = 0;
struct task *current;

void sched_init(void) {
	memset((unsigned char *)tasks, 0, sizeof(struct task) * MAX_TASKS);
	num_tasks = 1;
	current = &tasks[0];
	current->id = 0;
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

tid_t task_create(uint32_t eip) {
	if (num_tasks + 1 >= MAX_TASKS)
		return -1;
	struct task *new = &tasks[num_tasks];
	if (!new->stack) // Reuse existing stack if possible
		new->stack = kmalloc(PAGE_SIZE);
	else
		kprintf("Reusing stack at %h for task %i\n", new->stack, num_tasks);

	// Construct a fake initial stack that sets up task
	uint8_t *sptr = (uint8_t *)new->stack + PAGE_SIZE;
	sptr -= sizeof(struct new_task_stack);
	struct new_task_stack *stack = (void *)sptr;
	stack->ebp = stack->edi = stack->esi = stack->ebx = 0;
	stack->initial_return_addr = (uint32_t)task_init;
	stack->eip = eip; // Returns here after task_init iret
	stack->cs = 0x08; // Kernel code
	stack->eflags = 0x200; // Enable interrupts

	new->id = num_tasks;
	new->esp = (uint32_t)sptr;
	return num_tasks++;
}

void sched(void);
int task_kill(tid_t id) {
	if (id < 1)
		return -1;
	cli();
	if (id > num_tasks)
		return -1;
	--num_tasks;
	sti();
	return num_tasks;
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

static inline int find_next_runnable(void) {
	uint32_t ms = system_uptime_ms;
	for (int i = 1; i <= num_tasks; ++i) {
		int id = (current->id + i) % num_tasks;
		if (tasks[id].sleep_till && tasks[id].sleep_till > ms)
			continue;
		return id;
	}
	return current->id;
}
void sched(void) {
	int next_id = find_next_runnable();
	struct task *next = &tasks[next_id];
	if (current == next)
		return;
	struct task *prev = current;
	current = next;
	switch_to(prev, next);
}
