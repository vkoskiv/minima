#include <stdint.h>
#include <vkern.h>

typedef int tid_t;

void sched_init(void);

// NOTE: Prefer to call sleep() instead to yield, at least for now.
// sched() assumes interrupts are disabled,
void sched(void);

struct task;
tid_t task_create(int (*func)(void *), void *ctx, const char *name, int user_task);
int task_kill(struct task *t);
int wait_tid(tid_t task_id);
void dump_running_tasks(void);

enum task_state {
	ts_dead = 0,
	ts_runnable,
	ts_wait,
	ts_wait_task,
	ts_wait_semaphore,
	ts_sleeping,
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
	volatile enum task_state state;
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
	v_ilist waiting_on;
	int wait_retval; // Feels like a hack
	int32_t ticks;
	int32_t priority;
	struct vfs_node *cwd;
	// TODO:
	// int errno;
	// and then
	// #define errno (current->errno)?
	// Not really even sure if errno makes sense at kernel level.
};

#define TASK_STACK_PAGES 1
extern struct task *current;
