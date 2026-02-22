#ifndef _SYSCALLS_H_
#define _SYSCALLS_H_

#include "stdint.h"
#include "idt.h"

struct syscall {
	void *handler;
	uint8_t args;
};

extern struct syscall syscalls[];
void do_syscall(struct irq_regs regs);

#endif
