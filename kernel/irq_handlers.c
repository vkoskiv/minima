//
//  irq_handlers.c
//  xcode
//
//  Created by Valtteri on 25.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "io.h"
#include "idt.h"
#include "keyboard.h"

static inline void eoi(uint8_t irq) {
	if (irq >= 8)
		io_out8(PIC2_CMD, PIC_EOI);
	io_out8(PIC1_CMD, PIC_EOI);
}

void irq0_handler(void) {
	// System timer
	// Presumably the scheduler would be invoked here?
	eoi(0);
}

void irq1_handler(void) {
	// Keyboard
	uint8_t scancode = io_in8(0x60);
	received_scancode(scancode);
	eoi(1);
}

void irq2_handler(void) {
	eoi(2);
}

void irq3_handler(void) {
	eoi(3);
}

void irq4_handler(void) {
	eoi(4);
}

void irq5_handler(void) {
	eoi(5);
}

void irq6_handler(void) {
	eoi(6);
}

void irq7_handler(void) {
	eoi(7);
}

void irq8_handler(void) {
	// CMOS RTC
	eoi(8);
}

void irq9_handler(void) {
	eoi(9);
}

void irq10_handler(void) {
	eoi(10);
}

void irq11_handler(void) {
	eoi(11);
}

void irq12_handler(void) {
	eoi(12);
}

void irq13_handler(void) {
	// Math coprocessor
	eoi(13);
}

void irq14_handler(void) {
	eoi(14);
}

void irq15_handler(void) {
	eoi(15);
}
