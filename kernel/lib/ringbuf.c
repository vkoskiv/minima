#include <lib/ringbuf.h>
#include <utils.h>

void rb_initialize(struct ringbuf *rb, void *buf, size_t capacity) {
	rb->buf = buf;
	rb->cap = capacity;
	sem_init(&rb->queue, 1);
	sem_init(&rb->n_free, rb->cap);
	sem_init(&rb->n_used, 0);
}

void _rb_write(struct ringbuf *b, size_t size, void *data) {
	sem_pend(&b->n_free);
	sem_pend(&b->queue);
	memcpy(&b->buf[(b->head + 1) & (b->cap - 1)], data, size);
	b->head = (b->head + size) & (b->cap - 1);
	sem_post(&b->queue);
	sem_post(&b->n_used);
}

void _rb_read(struct ringbuf *b, size_t size, void *out) {
	sem_pend(&b->n_used);
	sem_pend(&b->queue);
	memcpy(out, &b->buf[(b->tail + 1) & (b->cap - 1)], size);
	b->tail = (b->tail + size) & (b->cap - 1);
	sem_post(&b->queue);
	sem_post(&b->n_free);
}
