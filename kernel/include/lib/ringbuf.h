#ifndef _RINGBUF_H_
#define _RINGBUF_H_

#include <stddef.h>
#include <sync.h>

struct ringbuf {
	struct semaphore queue, n_free, n_used;
	uint8_t *buf;
	uint32_t cap, head, tail;
};

void _rb_write(struct ringbuf *b, size_t size, void *data);
void _rb_read(struct ringbuf *b, size_t size, void *out);
/*
	These all return number of complete elements read/written
*/
void rb_initialize(struct ringbuf *rb, void *buf, size_t capacity);
#define rb_write(buf, val) _rb_write((buf), sizeof((val)), &(val))
#define rb_read(buf, out) _rb_read((buf), sizeof(*(out)), (uint8_t *)(out))

#endif
