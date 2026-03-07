#pragma once

#include <stddef.h>
#include <device.h>

struct char_dev {
	struct device base;
	int (*read)(char *buf, size_t n);
	int (*write)(char *buf, size_t n);
};

int read(struct char_dev *dev, char *buf, size_t n);
int write(struct char_dev *dev, char *buf, size_t n);
