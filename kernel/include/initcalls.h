//
// initcalls.h - a way to statically register functions to be called in
// stage1 early init. Relies on supporting machinery in linker.ld
//

#ifndef _INITCALLS_H_
#define _INITCALLS_H_

#include <vkern.h>

void run_initcalls(void);

#define add_initcall(func) \
void (*_initcall_##func)(void) __attribute__((section(".initcalls"))) = func

#endif
