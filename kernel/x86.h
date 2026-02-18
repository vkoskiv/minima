#ifndef _X86_H_
#define _X86_H_

#include "mman.h"

static inline virt_addr read_cr2(void) {
	virt_addr ret;
	asm volatile("movl %%cr2, %0" : "=r"(ret));
	return ret;
}

#define EFLAGS_IF        (0x1 << 9)

static inline uint32_t read_eflags(void) {
	uint32_t eflags;
	asm volatile("pushfl; popl %0" : "=r"(eflags));
	return eflags;
}

#endif
