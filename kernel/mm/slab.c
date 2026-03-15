//
// slab.c - slab memory allocator for allocations < PAGE_SIZE bytes
//
// I derived this implementation from first principles, based on my vague
// existing understanding of how slab allocators work, and a brief skim of the
// (also rather vague) Wikipedia page: https://en.wikipedia.org/wiki/Slab_allocation
//
// This allocator is effectively a layer between the page frame allocator (pfa.c)
// and kmalloc(), and it breaks the rather course 4KB allocations from pf_alloc()
// into caches (slabs) consisting of of smaller blocks (chunk).
//

#include <mm/slab.h>
#include <stddef.h>
#include <vkern.h>
#include <mm/pfa.h>
#include <console.h>
#include <kprintf.h>
#include <debug.h>
#include <sync.h>

#if DEBUG_SLAB == 1
#define dbg(...) kprintf(__VA_ARGS__)
#else
#define dbg(...)
#endif

static struct semaphore s_slab_sem = { 0 };

// TOOD: consider size 4 as well. Doable on 32 bit!
static const size_t chunk_sizes[] = {
	8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
};
#define N_SIZES (sizeof(chunk_sizes) / sizeof(chunk_sizes[0]))

static uint32_t wasted_bytes[N_SIZES] = { 0 };

static v_ilist slabs[N_SIZES] = { 0 };

struct chunk {
	struct chunk *next;
};

struct slab_meta {
	v_ilist linkage;
	uint8_t *slab;
	struct chunk *freelist;
	uint16_t free_slots;
};

static struct slab_meta bootstrap_meta = { 0 };

uint32_t round_up_pow2(size_t v) {
	uint32_t pow = 1;
	while (pow < v)
		pow <<= 1;
	return pow;
}

static void chunk_put(struct slab_meta *meta, struct chunk *chunk) {
	chunk->next = meta->freelist;
	meta->freelist = chunk;
	++meta->free_slots;
	// kprintf("%u freelist: %h, free_slots: %u\n", meta->slot_size, meta->freelist, meta->free_slots);
	dbg("put %u byte chunk %h from slab %h (meta %h)\n", meta->slot_size, chunk, meta->slab, meta);
}

static struct chunk *chunk_get(struct slab_meta *meta, uint16_t slot_size) {
	struct chunk *chunk = meta->freelist;
	meta->freelist = chunk->next;
	--meta->free_slots;
	// kprintf("%u freelist: %h, free_slots: %u\n", meta->slot_size, meta->freelist, meta->free_slots);
	memset((void *)chunk, 0, slot_size);
	dbg("got %u byte chunk %h from slab %h (meta %h)\n", slot_size, chunk, meta->slab, meta);
	return chunk;
}

static void prepare_slab(struct slab_meta *m, uint16_t slot_size) {
	m->linkage = V_ILIST_INIT(m->linkage);
	m->free_slots = 0;
	m->slab = pf_alloc();
	dbg("slab_meta%u(%h) slab is at: %h\n", m->slot_size, m, m->slab);
	m->freelist = NULL;
	for (size_t i = 0; i < (PAGE_SIZE / slot_size); ++i)
		chunk_put(m, (struct chunk *)(m->slab + (i * slot_size)));
}

static void *do_slab_alloc(size_t bytes);

// Allocate a slab and populate the freelist
static struct slab_meta *grab_a_slab(uint16_t slot_size) {
	struct slab_meta *m = do_slab_alloc(sizeof(*m));
	prepare_slab(m, slot_size);
	return m;
}

// There has to be a cleverer way than this, but I want to come up with it myself.
// This'll work for now.
static uint32_t size_idx(size_t bytes) {
	size_t rounded = round_up_pow2(bytes);
	switch (rounded) {
	case 8: return 0;
	case 16: return 1;
	case 32: return 2;
	case 64: return 3;
	case 128: return 4;
	case 256: return 5;
	case 512: return 6;
	case 1024: return 7;
	case 2048: return 8;
	case 4096: default: return 9;
	}
}

void slab_init(void) {
	for (size_t i = 0; i < N_SIZES; ++i)
		slabs[i] = V_ILIST_INIT(slabs[i]);

	sem_init(&s_slab_sem, 1, "slab");

	// This is needed to bootstrap the slab allocator. The slab_meta structures
	// used by the allocator are 20 bytes a pop, so preallocate a 32 byte slab
	// so the allocator can use itself to allocate them.
	struct slab_meta *m = &bootstrap_meta;
	prepare_slab(m, round_up_pow2(sizeof(*m)));
	v_ilist_prepend(&m->linkage, &slabs[size_idx(round_up_pow2(sizeof(*m)))]);
	kprintf("mm/slab: [");
	for (size_t i = 0; i < N_SIZES; ++i)
		kprintf("%u%s", chunk_sizes[i], i < N_SIZES - 1 ? "," : "]\n");
}

// FIXME: stash/cache free slot from previous alloc to shortcut this slow scan
static void *do_slab_alloc(size_t bytes) {
	// kprintf("slab_alloc(%u)\n", bytes);
	size_t size = size_idx(bytes);
	size_t slot_size = chunk_sizes[size];
	wasted_bytes[size] += slot_size - bytes;
	v_ilist *pos;
	v_ilist_for_each(pos, &slabs[size]) {
		struct slab_meta *m = v_ilist_get(pos, struct slab_meta, linkage);
		if (slot_size == round_up_pow2(sizeof(*m)) && m->freelist && !m->freelist->next) {
			// kprintf("%h free_slots: %u, eagerly preallocating new 32 byte slab\n", m, m->free_slots);
			struct slab_meta *eager = (void *)chunk_get(m, slot_size);
			prepare_slab(eager, slot_size);
			v_ilist_prepend(&eager->linkage, &slabs[size]);
			return chunk_get(eager, slot_size);
		}
		if (m->free_slots)
			return chunk_get(m, slot_size);
	}
	// No slot, prepare a new slab
	struct slab_meta *new = grab_a_slab(slot_size);
	v_ilist_prepend(&new->linkage, &slabs[size]);
	return chunk_get(new, slot_size);
}

/*
	Dumb O(n^2) search because I can't come up with a better solution rn.
	FIXME: Come up with a better solution.
*/
static void do_slab_free(void *ptr) {
	for (size_t size = 0; size < N_SIZES; ++size) {
		v_ilist *pos;
		v_ilist_for_each(pos, &slabs[size]) {
			struct slab_meta *m = v_ilist_get(pos, struct slab_meta, linkage);
			if (m->slab == (void *)(((uint32_t)ptr) & ~0xFFF)) {
				chunk_put(m, (struct chunk *)ptr);
				// kprintf("slab_free(%h) (%u)\n", ptr, m->slot_size);
				if (m->free_slots == (PAGE_SIZE / chunk_sizes[size])) {
					v_ilist_remove(&m->linkage);
					pf_free(m->slab);
					do_slab_free(m);
					// TODO: that check is awkward, maybe track used slots instead?
					// or remove free_slots entirely and work that stuff out just with
					// the freelist, like pfa.c already does!
				}
				return;
			}
		}
	}
}

void *slab_alloc(size_t bytes) {
	sem_pend(&s_slab_sem);
	void *blk = do_slab_alloc(bytes);
	sem_post(&s_slab_sem);
	return blk;
}

void slab_free(void *ptr) {
	sem_pend(&s_slab_sem);
	do_slab_free(ptr);
	sem_post(&s_slab_sem);
}

// Debug code below this point

#include <timer.h>

#define PER_LINE 8

volatile uint32_t kill_idx = 0;

static void dump_slab_list(v_ilist *slab_list, uint16_t slot_size) {
	if (v_ilist_is_empty(slab_list))
		return;
	uint32_t idx = 0;
	v_ilist *pos;
	v_ilist_for_each(pos, slab_list) {
		struct slab_meta *m = v_ilist_get(pos, struct slab_meta, linkage);
		// if (!m->slot_size) {
		// 	panic("!m->slot_size");
		// }
		size_t total_slots = PAGE_SIZE / slot_size;
		kprintf("[%u]", total_slots - m->free_slots);
		if (++idx % PER_LINE == 0)
			kprintf("\n\t");
	}
	kput('\n');
}

static int dump_slab_counts(void *ctx) {
	(void)ctx;
	sem_pend(&s_slab_sem);
	for (size_t i = 0; i < N_SIZES; ++i)
		kprintf("%u: %u ", chunk_sizes[i], v_ilist_count(&slabs[i]));
	kput('\n');
	sem_post(&s_slab_sem);
	return 0;
}

static int dump_wasted_bytes(void *ctx) {
	(void)ctx;
	sem_pend(&s_slab_sem);
	for (size_t i = 0; i < N_SIZES; ++i)
		kprintf("%u: %u ", chunk_sizes[i], wasted_bytes[i]);
	kput('\n');
	sem_post(&s_slab_sem);
	return 0;
}

static int dump_slab_counts_loop(void *ctx) {
	(void)ctx;
	uint32_t kidx = kill_idx;
	while (kidx == kill_idx) {
		dump_slab_counts(ctx);
		sleep(1000);
	}
	return 0;
}

int dump_slabs(void *ctx) {
	int select = (int)ctx;
	sem_pend(&s_slab_sem);
	for (int size = 0; size < (int)N_SIZES; ++size) {
		uint16_t slot_size = chunk_sizes[size];
		if (select > -1 && size != select)
			continue;
		if (v_ilist_is_empty(&slabs[size]))
			continue;
		kprintf("%u[%u]:\n\t", slot_size, v_ilist_count(&slabs[size]));
		dump_slab_list(&slabs[size], slot_size);
	}
	sem_post(&s_slab_sem);
	return 0;
}

static void *temp_allocs_32[256] = { 0 };
static size_t temp_allocs_32_idx = 0;
static size_t freeidx = 0;
// #define ALLOCS 32
static int alloc(void *ctx) {
	size_t bytes = (size_t)ctx;
	// void *ptrs[ALLOCS] = { 0 };
	// for (size_t i = 0; i < ALLOCS; ++i) {
	// 	ptrs[i] = slab_alloc(bytes);
	// 	dump_slabs((void *)size_idx(bytes));
	// }

	// for (size_t i = 0; i < ALLOCS; ++i) {
	// 	slab_free(ptrs[i]);
	// 	dump_slabs((void *)size_idx(bytes));
	// }
	void *ptr = slab_alloc(bytes);
	if (bytes == 32 && temp_allocs_32_idx < 256) {
		temp_allocs_32[temp_allocs_32_idx++] = ptr;
	}
	dump_slabs((void *)-1);
	return 0;
}

static int leak128(void *ctx) {
	size_t bytes = (size_t)ctx;
	for (size_t i = 0; i < 128; ++i)
		slab_alloc(bytes);
	dump_slabs((void *)-1);
	return 0;
}

static int free(void *ctx) {
	size_t bytes = (size_t)ctx;
	if (bytes == 32 && temp_allocs_32_idx)
		slab_free(temp_allocs_32[--temp_allocs_32_idx]);
	else
		kprintf("TODO %u\n", bytes);
	dump_slabs((void *)-1);
	return 0;
}

static int freerev(void *ctx) {
	size_t bytes = (size_t)ctx;
	if (bytes == 32 && temp_allocs_32_idx)
		slab_free(temp_allocs_32[freeidx++]);
	else
		kprintf("TODO %u\n", bytes);
	dump_slabs((void *)-1);
	return 0;
}

static int kill_tasks(void *ctx) {
	(void)ctx;
	kill_idx++;
	return 0;
}

struct cmd_list slab_debug = {
	.name = "slab_debug",
	.cmds = {
		{ {}, 0, 1, (void *)-1, TASK(dump_slabs), "dump slabs", 'd', 0 },
		{ {}, 0, 1, (void *)-1, TASK(dump_slab_counts), "dump slab counts", 'f', 0 },
		{ {}, 0, 1, (void *)-1, TASK(dump_wasted_bytes), "dump wasted bytes", 'w', 0 },
		{ {}, 0, -1, NULL, TASK(dump_slab_counts_loop), "loop dump slab counts", 'g', 0 },
		{ {}, 0,  1, NULL,      TASK(kill_tasks), "kill running test tasks", 'x',  0  },
		{ {}, 0, 1, (void *)8,    TASK(alloc), "allocate 8",    '1', 0 },
		{ {}, 0, 1, (void *)16,   TASK(alloc), "allocate 16",   '2', 0 },
		{ {}, 0, 1, (void *)32,   TASK(alloc), "allocate 32",   '3', 0 },
		{ {}, 0, 1, (void *)32,   TASK(free), "free 32",        'c', 0 },
		{ {}, 0, 1, (void *)32,   TASK(freerev), "free 32rev",        'v', 0 },
		{ {}, 0, 1, (void *)32,   TASK(leak128), "leak 128x32",   'e', 0 },
		{ {}, 0, 1, (void *)64,   TASK(alloc), "allocate 64",   '4', 0 },
		{ {}, 0, 1, (void *)128,  TASK(alloc), "allocate 128",  '5', 0 },
		{ {}, 0, 1, (void *)256,  TASK(alloc), "allocate 256",  '6', 0 },
		{ {}, 0, 1, (void *)512,  TASK(alloc), "allocate 512",  '7', 0 },
		{ {}, 0, 1, (void *)1024, TASK(alloc), "allocate 1024", '8', 0 },
		{ {}, 0, 1, (void *)2048, TASK(alloc), "allocate 2048", '9', 0 },
		{ {}, 0, 1, (void *)4096, TASK(alloc), "allocate 4096", 'o', 0 },
		// { {}, 0, 1, (void *)8,    TASK(free), "free 8",    '1', 0 },
		// { {}, 0, 1, (void *)16,   TASK(free), "free 16",   '2', 0 },
		// { {}, 0, 1, (void *)32,   TASK(free), "free 32",   '3', 0 },
		// { {}, 0, 1, (void *)64,   TASK(free), "free 64",   '4', 0 },
		// { {}, 0, 1, (void *)128,  TASK(free), "free 128",  '5', 0 },
		// { {}, 0, 1, (void *)256,  TASK(free), "free 256",  '6', 0 },
		// { {}, 0, 1, (void *)512,  TASK(free), "free 512",  '7', 0 },
		// { {}, 0, 1, (void *)1024, TASK(free), "free 1024", '8', 0 },
		// { {}, 0, 1, (void *)2048, TASK(free), "free 2048", '9', 0 },
		// { {}, 0, 1, (void *)4096, TASK(free), "free 4096", 'o', 0 },
		{ 0 },
	}
};
