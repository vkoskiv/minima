#include "stdint.h"

typedef int tid_t;

void sched_init(void);

tid_t task_create(uint32_t eip);
int task_kill(tid_t task_id);

struct task {
	uint32_t id;
	uint32_t esp; // Note: switch_to hard-codes offset of this member
	uint32_t sleep_till;
	void *stack;
};

#define MAX_TASKS 2000
extern struct task tasks[MAX_TASKS];
extern struct task *current;
extern tid_t num_tasks;
