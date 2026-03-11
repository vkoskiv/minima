#ifndef _DEV_CHAR_H_
#define _DEV_CHAR_H_

#include <stddef.h>
#include <device.h>

struct dev_char {
	struct device base;
	int (*read)(struct device *dev, char *buf, size_t n);
	int (*write)(struct device *dev, char *buf, size_t n);
};

int read(struct dev_char *dev, char *buf, size_t n);
int write(struct dev_char *dev, char *buf, size_t n);

int dev_char_register(struct dev_char *dev);
int dev_char_unregister(struct dev_char *dev);

struct dev_char *dev_char_open(const char *name);
void dev_char_close(struct dev_char *dev);

extern v_ilist char_devices;

#endif
