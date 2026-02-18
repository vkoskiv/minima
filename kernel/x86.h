#ifndef _X86_H_
#define _X86_H_

#include "mman.h"

static inline virt_addr read_cr2(void) {
	virt_addr ret;
	asm volatile(
		"mov %[ret], cr2"
		: [ret]"=r"(ret)
		: /* No inputs */
		: /* No clobbers */
	);
	return ret;
}

#define EFLAGS_IF        (0x1 << 9)

static inline uint32_t read_eflags(void) {
	uint32_t eflags;
	asm volatile(
		"pushf;"
		"pop %0"
		: "=r"(eflags)
		: /* No inputs */
		: /* No clobbers */
	);
	return eflags;
}

// Stop interrupts
static inline void cli(void) {
	asm("cli");
}

// Restore interrupts
static inline void sti(void) {
	asm("sti");
}

void cli_push(void);
void cli_pop(void);

static inline void halt(void) {
	cli();
	for (;;)
		asm("hlt");
}

#endif
