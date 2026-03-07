#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <vkern.h>

struct device {
	const char *name;
	v_ilist linkage;
	void *ctx;
};

#endif
