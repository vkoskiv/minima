#ifndef _DEV_CHAR_H_
#define _DEV_CHAR_H_

#include <stddef.h>
#include <device.h>

struct dev_char {
	struct device base;
	int (*read)(struct device *dev, char *buf, size_t n);
	int (*write)(struct device *dev, const char *buf, size_t n);
};

#endif
