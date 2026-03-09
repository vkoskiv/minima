#include <sync.h>
#include <sched.h>
#include <debug.h>
#include <console.h>
#include <x86.h>
#include <kprintf.h>

#if DEBUG_SYNC == 1
#include <kprintf.h>
#define dbg(...) kprintf(__VA_ARGS__)
#else
#define dbg(...)
#endif

void sem_init(struct semaphore *s, int value) {
	s->waiters = V_ILIST_INIT(s->waiters);
	s->count = value;
	dbg("[%s(%i)] sem_init(%h, %i)\n", current->name, current->id, s, value);
}

void sem_post(struct semaphore *s) {
	dbg("[%s(%i)] sem_post %i -> %i, ", current->name, current->id, s->count, s->count + 1);
	++s->count;
	if (v_ilist_is_empty(&s->waiters)) {
		dbg("no waiters\n");
	} else {
		struct task *waiter = v_ilist_get_first(&s->waiters, struct task, linkage);
		dbg("waking %s(%i)\n", waiter->name, waiter->id);
		v_ilist_remove(&waiter->linkage);
		task_wake(waiter);
	}
}

void sem_pend(struct semaphore *s) {
	dbg("[%s(%i)] sem_pend at %i, ", current->name, current->id, s->count);
	for (;;) {
		int val = s->count;
		if (val) {
			dbg("trying lock, ");
			if (cmpxchg(&s->count, val, val - 1) == val) {
				dbg("cmpxchg(&s->count, %i, %i) -> %i, returning\n", val, val - 1, val);
				return;
			}
		}
		dbg("going to sleep\n");
		cli_push();
		current->state = ts_waiting;
		v_ilist_prepend(&current->linkage, &s->waiters);
		sched(); // take this task off the CPU and let other tasks run
		// sched returning means we were unblocked by sem_post() elsewhere.
		dbg("[%s(%i)] sem_pend awoken, s->count = %i\n", current->name, current->id, s->count);
		cli_pop();
	}
}

static int s_dbg_sem_init = 0;
static struct semaphore s_debug_sem = { 0 };

static int dbg_setup(void *ctx) {
	struct semaphore *s = ctx;
	sem_init(s, 0);
	s_dbg_sem_init = 1;
	return 0;
}

#define DBG_MAX_CONSUMERS 32
static uint8_t s_idx = 0;
static struct dbg_consumer {
	tid_t tid;
	uint32_t loops;
} s_debug_consumers[DBG_MAX_CONSUMERS] = { 0 };

int dbg_stop_consumer(void *ctx) {
	(void)ctx;
	if (!s_idx)
		return -1;
	--s_idx;
	kprintf("stopping %i\n", s_debug_consumers[s_idx].tid);
	s_debug_consumers[s_idx].tid = -1;
	return 0;
}

int dbg_consumer(void *ctx) {
	if (s_idx >= DBG_MAX_CONSUMERS)
		return -1;
	if (!s_dbg_sem_init)
		dbg_setup(ctx);
	s_debug_consumers[s_idx].tid = current->id;
	struct dbg_consumer *c = &s_debug_consumers[s_idx++];
	struct semaphore *s = ctx;

	c->loops = 0;
	while (c->tid != -1) {
		sem_pend(s);
		++c->loops;
		dbg("[%s(%i)] ++loops = %i\n", current->name, current->id, c->loops);
	}
	dbg("[%s(%i)] exiting, loops = %i\n", current->name, current->id, c->loops);

	return 0;
}

int dbg_produce(void *ctx) {
	if (!s_dbg_sem_init)
		dbg_setup(ctx);
	struct semaphore *s = ctx;
	sem_post(s);
	return 0;
}

int dbg_dump_sem(void *ctx) {
	if (!s_dbg_sem_init)
		dbg_setup(ctx);
	struct semaphore *s = ctx;
	kprintf("s->count = %i", s->count);
	size_t n = v_ilist_count(&s->waiters);
	if (n) {
		kprintf(", %i waiters:\n", n);
		v_ilist *pos;
		v_ilist_for_each(pos, &s->waiters) {
			struct task *t = v_ilist_get(pos, struct task, linkage);
			uint32_t loops = 99999;
			for (int i = 0; i < DBG_MAX_CONSUMERS; ++i) {
				if (s_debug_consumers[i].tid == t->id)
					loops = s_debug_consumers[i].loops;
			}
			kprintf("\t%s(%i) (%u loops)\n", t->name, t->id, loops);
		}
	} else {
		kprintf("\n");
	}
	return 0;
}

struct cmd_list sync_debug = {
	.name = "sync_debug",
	.cmds = {
		{ {}, 0, 1, &s_debug_sem, TASK(dbg_setup), "setup semaphore", 's', 0 },
		{ {}, 0, 0, &s_debug_sem, TASK(dbg_consumer), "start consumer", 'c', 0 },
		{ {}, 0, 0, NULL, TASK(dbg_stop_consumer), "stop consumer", 'x', 0 },
		{ {}, 0, 0, &s_debug_sem, TASK(dbg_produce), "produce one", '1', 0 },
		{ {}, 0, 1, &s_debug_sem, TASK(dbg_dump_sem), "dump semaphore", 'd', 0 },
		{ 0 },
	}
};
