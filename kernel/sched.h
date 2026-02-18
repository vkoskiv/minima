#include "stdint.h"
#include "vkern.h"

typedef int tid_t;

void sched_init(void);

// NOTE: Prefer to call sleep() instead to yield, at least for now.
// sched() assumes interrupts are disabled,
void sched(void);

tid_t task_create(void (*func)(void), const char *name);
int task_kill(tid_t task_id);
void dump_running_tasks(void);

struct task {
//!//!//!//!//!//!//!//!//
	tid_t id;           // <- switch_to() relies on
	uint32_t esp;       //    offset/order of these
//!//!//!//!//!//!//!//!//
	uint32_t sleep_till;
	void *stack;
	void *redzone_top;
	const char *name;
	void (*entry)(void);
	v_ilist linkage;
};

#define MAX_TASKS 2000
#define TASK_STACK_PAGES 1
extern struct task *current;
