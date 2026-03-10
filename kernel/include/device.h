#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <vkern.h>

struct device {
	const char *name;
	v_ilist linkage;
	uint32_t refs;
	void *ctx;
};

#endif
