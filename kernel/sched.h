#include "stdint.h"
#include "vkern.h"

typedef int tid_t;

void sched_init(void);

tid_t task_create(void (*func)(void), const char *name);
int task_kill(tid_t task_id);

struct task {
	tid_t id;
	uint32_t esp; // Note: switch_to hard-codes offset of this member
	uint32_t sleep_till;
	void *stack;
	const char *name;
	v_ilist linkage;
};

#define MAX_TASKS 2000
extern struct task *current;
