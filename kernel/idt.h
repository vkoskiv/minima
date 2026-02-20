//
//  idt.h
//
//  Created by Valtteri on 25.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#ifndef _IDT_H_
#define _IDT_H_

#include "stdint.h"

void eoi(unsigned char irq);
void idt_init(void);

int dump_irq_counts(void *ctx);

extern uint32_t irq_counts[];
extern const uint16_t num_irqs;

#endif
