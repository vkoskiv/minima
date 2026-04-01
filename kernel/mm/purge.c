#include <mm/purge.h>
#include <vkern.h>
#include <kmalloc.h>
#include <assert.h>
#include <sync.h>

struct purgeable {
	void *buf;
	size_t bytes;
	v_ilist linkage;
	struct semaphore lock;
	void (*on_purge)(void *ctx);
	void *on_purge_ctx;
};

static v_ilist s_purgeable = V_ILIST_INIT(s_purgeable);
static SEMAPHORE(s_list, 1);

// FIXME: should maybe defer actual buf alloc to first purgeable_get() call, with was_purged = 1?
struct purgeable *purgeable_alloc(size_t bytes, void (*on_purge)(void *ctx), void *on_purge_ctx) {
	struct purgeable *p = kmalloc(sizeof(*p));
	if (!p) {
		// TODO: try to purge memory here too.
		return NULL;
	}
	p->buf = kmalloc(bytes);
	p->bytes = bytes;
	p->on_purge = on_purge;
	p->on_purge_ctx = on_purge_ctx;
	sem_init(&p->lock, 1, "purgeable_buf");
	sem_pend(&s_list);
	{
		v_ilist_prepend(&p->linkage, &s_purgeable);
	}
	sem_post(&s_list);
	return p;
}

static size_t mem_purge(struct purgeable *p) {
	assert(!v_ilist_is_head(&s_purgeable, &p->linkage));
	sem_pend(&p->lock);
	if (!p->buf) {
		sem_post(&p->lock);
		return 0;
	}
	if (p->on_purge)
		p->on_purge(p->on_purge_ctx);
	kfree(p->buf);
	p->buf = NULL;
	sem_post(&p->lock);
	return p->bytes;
}

// free and unregister purgeable region
void purgeable_free(struct purgeable *p) {
	assert(p);
	sem_pend(&p->lock);
	assert(p->bytes);
	sem_pend(&s_list);
	{
		assert(!v_ilist_is_head(&s_purgeable, &p->linkage));
		v_ilist_remove(&p->linkage);
	}
	sem_post(&s_list);
	if (p->buf)
		kfree(p->buf);
	kfree(p);
}

void *purgeable_get(struct purgeable *p, int *was_purged) {
	assert(p);
	sem_pend(&p->lock);
	if (was_purged)
		*was_purged = p->buf ? 0 : 1;
	if (!p->buf)
		p->buf = kmalloc(p->bytes);
	return p->buf;
}

void purgeable_put(void *ptr) {
	assert(ptr);
	// FIXME: slow linear scan
	struct purgeable *p = NULL;
	v_ilist *pos;
	v_ilist_for_each(pos, &s_purgeable) {
		struct purgeable *test = v_ilist_get(pos, struct purgeable, linkage);
		if (test->buf == ptr) {
			p = test;
			break;
		}
	}
	if (!p)
		return;
	sem_post(&p->lock);
}

// TODO: Be a bit smarter about this, rather than just purging stuff until
// reaching minimum
size_t purgeable_purge(size_t bytes) {
	size_t total_purged = 0;
	v_ilist *pos;
	sem_pend(&s_list);
	v_ilist_for_each(pos, &s_purgeable) {
		struct purgeable *p = v_ilist_get(pos, struct purgeable, linkage);
		total_purged += mem_purge(p);
		if (bytes && total_purged >= bytes)
			break;
	}
	sem_post(&s_list);
	return total_purged;
}
