//
//  idt.h
//
//  Created by Valtteri on 25.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#ifndef _IDT_H_
#define _IDT_H_

#include <stdint.h>

void idt_init(void);

int dump_irq_counts(void *ctx);

struct irq_regs {
	// push/pop in irq_common:
	uint32_t gs, fs, es, ds;
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	uint32_t irq_num;
	// pushed by CPU for E_IRQ entries, our IRQ entries push 0
	uint32_t error;
	// pushed by the CPU before invoking our irq handler:
	void (*eip)(void);
	uint32_t cs, eflags; // usermode_esp, usermode_ss;?
};
// FIXME: Check the above against page 159 of i386 manual, looks
// kind of weird to me atm.

#define IRQ0_OFFSET 0x20

int attach_irq(int irq, void (*handler)(struct irq_regs), const char *name);

extern uint32_t irq_counts[];
extern const uint16_t num_irqs;

#endif
