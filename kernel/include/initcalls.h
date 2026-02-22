#ifndef _INITCALLS_H_
#define _INITCALLS_H_

#include <vkern.h>

void run_initcalls(void);

#define add_initcall(func) \
void (*_initcall_##func)(void) __attribute__((section(".initcalls"))) = func

#endif
