#ifndef _SYNC_H_
#define _SYNC_H_

#include <vkern.h>

/*
	Counting semaphore. sem_pend() blocks if count reaches 0
*/

struct semaphore {
	volatile int count;
	v_ilist waiters;
	const char *name;
};

#define SEMAPHORE(sem_name, val) struct semaphore (sem_name) = { .count = (val), .waiters = V_ILIST_INIT((sem_name).waiters), .name = (#sem_name) }

// FIXME: s/sem_/sema_
void sem_init(struct semaphore *s, int value, const char *name);
void sem_post(struct semaphore *s);
void sem_pend(struct semaphore *s);

#endif
