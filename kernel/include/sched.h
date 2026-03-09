#include <stdint.h>
#include <vkern.h>

typedef int tid_t;

void sched_init(void);

// NOTE: Prefer to call sleep() instead to yield, at least for now.
// sched() assumes interrupts are disabled,
void sched(void);

tid_t task_create(int (*func)(void *), void *ctx, const char *name, int user_task);
int task_kill(tid_t task_id);
int wait_tid(tid_t task_id);
void dump_running_tasks(void);

enum task_state {
	ts_dead = 0,
	ts_runnable,
	ts_waiting,
	ts_stopping,
};

struct task {
//!//!//!//!//!//!//!//!//
	tid_t id;           // <- switch_to() relies on
	uint32_t k_esp;     //    offset/order of these
//!//!//!//!//!//!//!//!//
	uint32_t u_esp;
	uint32_t sleep_till;
	void *stack_kernel;
	void *stack_user;
	void *redzone_top;
	enum task_state state;
	int cli_depth;
	int cli_int_enabled;
	int sti_depth;
	int sti_int_disabled;
	v_ilist waiters;
	int ret;
	const char *name;
	int (*entry)(void *);
	void *ctx;
	v_ilist linkage;
};

void task_wake(struct task *t);

#define TASK_STACK_PAGES 1
extern struct task *current;
