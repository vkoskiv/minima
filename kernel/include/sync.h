#ifndef _SYNC_H_
#define _SYNC_H_

#include <vkern.h>

/*
	Counting semaphore. sem_pend() blocks if count reaches 0
*/

struct semaphore {
	volatile int count;
	v_ilist waiters;
};

// FIXME: s/sem_/sema_
void sem_init(struct semaphore *s, int value);
void sem_post(struct semaphore *s);
void sem_pend(struct semaphore *s);

#endif
