#include <sync.h>
#include <sched.h>
#include <debug.h>
#include <console.h>
#include <x86.h>
#include <kprintf.h>

#if DEBUG_SYNC == 1
#include <kprintf.h>
#define dbg(...) kprintf_noserial(__VA_ARGS__)
#else
#define dbg(...)
#endif

void sem_init(struct semaphore *s, int value, const char *name) {
	s->waiters = V_ILIST_INIT(s->waiters);
	s->count = value;
	s->name = name;
	dbg("[%s(%i)] sem_init(%h, %i, '%s')\n", current->name, current->id, s, s->count, s->name);
}

void sem_post(struct semaphore *s) {
	dbg("[%s(%i)] sem_post(%s) %i -> %i, ", current->name, current->id, s->name, s->count, s->count + 1);
	asm volatile(
		"lock inc %[count];"
		: [count]"+m"(s->count)
		: /* No inputs  */
		: "memory"
	);
	if (v_ilist_is_empty(&s->waiters)) {
		dbg("no waiters\n");
	} else {
		struct task *waiter = v_ilist_get_first(&s->waiters, struct task, waiting_on);
		dbg("waking %s(%i)\n", waiter->name, waiter->id);
		v_ilist_remove(&waiter->waiting_on);
		waiter->state = ts_runnable;
	}
}

#define TRIES 2

void sem_pend(struct semaphore *s) {
	dbg("[%s(%i)] sem_pend(%s) %i ", current->name, current->id, s->name, s->count);
	for (;;) {
		int val;
		// Hack: I was running into a case where the serial ringbuffer consumer went to sleep
		// due to a stale value in val, after the producer had gone to sleep due to a full
		// ringbuffer. This doesn't feel like a very robust fix, but it does seem to prevent
		// deadlocks.
		for (int t = 0; t < TRIES; ++t) {
			if ((val = s->count)) {
				if (cmpxchg(&s->count, val, val - 1) == val) {
					dbg("-> %i (try %i)\n", val - 1, t);
					return;
				}
			}
		}
		dbg("[%s(%i)] sem_pend(%s) %i, going to sleep. val: %i\n", current->name, current->id, s->name, s->count, val);
		cli_push();
		current->state = ts_wait_semaphore;
		v_ilist_prepend(&current->waiting_on, &s->waiters);
		sched(); // take this task off the CPU and let other tasks run
		// sched returning means we were unblocked by sem_post() elsewhere.
		dbg("[%s(%i)] sem_pend(%s) awoken, s->count = %i\n", current->name, current->id, s->name, s->count);
		cli_pop();
	}
}

static int s_dbg_sem_init = 0;
static struct semaphore s_debug_sem = { 0 };

static int dbg_setup(void *ctx) {
	struct semaphore *s = ctx;
	sem_init(s, 0, "dbg_sem");
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
			struct task *t = v_ilist_get(pos, struct task, waiting_on);
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

const struct command sync_debug = SUBMENU("s&ync debug",
	CMD("set&up semaphore", dbg_setup, &s_debug_sem),
	JOB("st&art consumer", dbg_consumer, &s_debug_sem),
	JOB("st&op consumer", dbg_stop_consumer, NULL),
	JOB("&produce one", dbg_produce, &s_debug_sem),
	CMD("&dump semaphore", dbg_dump_sem, &s_debug_sem),
);
