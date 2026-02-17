#ifndef _ASSERT_H
#define _ASSERT_H

#include "panic.h"

#define assert(expr) \
do { \
	if (!(expr)) \
		panic("Assertion Failed: \"%s\"", #expr); \
} while (0)

#endif
