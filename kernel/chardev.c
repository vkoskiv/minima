#include <chardev.h>
// TODO: errno.h
// for now I'll just return -1
int read(struct char_dev *dev, char *buf, size_t n) {
	if (!dev->read)
		return -1;
	return dev->read(buf, n);
}

int write(struct char_dev *dev, char *buf, size_t n) {
	if (!dev->write)
		return -1;
	return dev->write(buf, n);
}
