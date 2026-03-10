#ifndef _RINGBUF_H_
#define _RINGBUF_H_

#include <x86.h>

struct ringbuf {
	uint8_t *buf;
	uint32_t cap, head, tail;
};

int _rb_write(struct ringbuf *b, size_t n, size_t size, void *data);
int _rb_read(struct ringbuf *b, size_t n, size_t size, void *out);
/*
	These all return number of complete elements read/written
*/
#define rb_write(buf, val) _rb_write((buf), 1, sizeof((val)), &(val))
#define rb_write_n(buf, vals, n) _rb_write((buf), (n), sizeof(*(val)), (val))
#define rb_read(buf, out) _rb_read((buf), 1, sizeof(*(out)), (uint8_t *)(out))
#define rb_read_n(buf, out, n) _rb_read((buf), (n), sizeof(*(out)), (uint8_t *)(out))

#endif
