//
// chardev.c - character special devices
//

#include <drv.h>
#include <fs/dev_char.h>
#include <types.h>
#include <errno.h>
#include <fs/dev.h>

static ssize_t zero_read(struct device *dev, char *buf, size_t n) {
	(void)dev;
	memset(buf, 0, n);
	return n;
}

static ssize_t null_read(struct device *dev, char *buf, size_t n) {
	(void)dev;
	(void)buf;
	(void)n;
	return 0;
}
static ssize_t null_write(struct device *dev, const char *buf, size_t n) {
	(void)dev;
	(void)buf;
	return n;
}

static ssize_t full_write(struct device *dev, const char *buf, size_t n) {
	(void)dev;
	(void)buf;
	(void)n;
	return -ENOSPC;
}

static const struct dev_char devices[] = {
	{
		.base.name = "null",
		.read = null_read,
		.write = null_write,
	},
	{
		.base.name = "zero",
		.read = zero_read,
		.write = null_write,
	},
	{
		// FIXME: full(4) manpage says seeks on /dev/full always succeed, but I was under
		// the impression that seek isn't really a thing on character devices?
		.base.name = "full",
		.read = zero_read,
		.write = full_write,
	},
};

static int probe(v_ma *a) {
	(void)a;
	for (size_t i = 0; i < (sizeof(devices) / sizeof(devices[0])); ++i)
		devfs_register_char(&devices[i]);
	return 0;
}

struct driver chardev = {
	.name = "chardev",
	.probe = probe,
};

register_driver(chardev);
