//
//  idt.h
//  xcode
//
//  Created by Valtteri on 25.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#define PIC1     0x20
#define PIC2     0xA0
#define PIC1_CMD PIC1
#define PIC2_CMD PIC2
#define PIC1_DAT (PIC1+1)
#define PIC2_DAT (PIC2+1)
#define PIC_EOI  0x20

void idt_init(void);

// Stop interrupts
void cli(void);

// Restore interrupts
void sti(void);
