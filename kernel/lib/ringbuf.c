#include <lib/ringbuf.h>
#include <utils.h>

int _rb_write(struct ringbuf *b, size_t n, size_t size, void *data) {
	cli_push();
	size_t bytes_avail = (b->tail - b->head - 1) & (b->cap - 1);
	size_t elems_avail = bytes_avail / size;
	if (!elems_avail) {
		cli_pop();
		return 0;
	}
	if (elems_avail < n)
		n = elems_avail;
	memcpy(&b->buf[(b->head + 1) & (b->cap - 1)], data, n * size);
	b->head = (b->head + (n * size)) & (b->cap - 1);
	cli_pop();
	return n;
}

int _rb_read(struct ringbuf *b, size_t n, size_t size, void *out) {
	cli_push();
	size_t bytes_avail = (b->head - b->tail) & (b->cap - 1);
	size_t elems_avail = bytes_avail / size;
	if (!elems_avail) {
		cli_pop();
		return 0;
	}
	if (elems_avail < n)
		n = elems_avail;
	memcpy(out, &b->buf[(b->tail + 1) & (b->cap - 1)], (n * size));
	b->tail = (b->tail + (n * size)) & (b->cap - 1);
	cli_pop();
	return n;
}
