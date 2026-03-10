#ifndef _CHARDEV_H_
#define _CHARDEV_H_

#include <stddef.h>
#include <device.h>

struct char_dev {
	struct device base;
	int (*read)(struct device *dev, char *buf, size_t n);
	int (*write)(struct device *dev, char *buf, size_t n);
};

int read(struct char_dev *dev, char *buf, size_t n);
int write(struct char_dev *dev, char *buf, size_t n);

int chardev_register(struct char_dev *dev);
int chardev_unregister(struct char_dev *dev);

struct char_dev *chardev_open(const char *name);
void chardev_close(struct char_dev *dev);

extern v_ilist char_devices;

#endif
