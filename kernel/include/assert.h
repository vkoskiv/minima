#ifndef _ASSERT_H
#define _ASSERT_H

#include <panic.h>
#include <debug.h>

#define NORETURN 0

#if DEBUG_ENABLE_ASSERTIONS == 1

#define assert(expr) \
do { \
	if (!(expr)) \
		panic("Assertion Failed: \"%s\"", #expr); \
} while (0)

#else

#define assert(expr)

#endif /* DEBUG_ENABLE_ASSERTIONS == 1 */

#endif
