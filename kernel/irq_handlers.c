//
//  irq_handlers.c
//  xcode
//
//  Created by Valtteri on 25.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "io.h"
#include "terminal.h"

void irq0_handler(void) {
	// System timer
	io_out8(0x20, 0x20); //EOI
}

void irq1_handler(void) {
	// Keyboard
	uint8_t scancode = io_in8(0x60);
	kprint("Keyboard interrupt triggered.\n");
	io_out8(0x20, 0x20); //EOI
}

void irq2_handler(void) {
	io_out8(0x20, 0x20); //EOI
}

void irq3_handler(void) {
	io_out8(0x20, 0x20); //EOI
}

void irq4_handler(void) {
	io_out8(0x20, 0x20); //EOI
}

void irq5_handler(void) {
	io_out8(0x20, 0x20); //EOI
}

void irq6_handler(void) {
	io_out8(0x20, 0x20); //EOI
}

void irq7_handler(void) {
	io_out8(0x20, 0x20); //EOI
}

void irq8_handler(void) {
	// CMOS RTC
	io_out8(0xA0, 0x20);
	io_out8(0x20, 0x20); //EOI
}

void irq9_handler(void) {
	io_out8(0xA0, 0x20);
	io_out8(0x20, 0x20); //EOI
}

void irq10_handler(void) {
	io_out8(0xA0, 0x20);
	io_out8(0x20, 0x20); //EOI
}

void irq11_handler(void) {
	io_out8(0xA0, 0x20);
	io_out8(0x20, 0x20); //EOI
}

void irq12_handler(void) {
	io_out8(0xA0, 0x20);
	io_out8(0x20, 0x20); //EOI
}

void irq13_handler(void) {
	// Math coprocessor
	io_out8(0xA0, 0x20);
	io_out8(0x20, 0x20); //EOI
}

void irq14_handler(void) {
	io_out8(0xA0, 0x20);
	io_out8(0x20, 0x20); //EOI
}

void irq15_handler(void) {
	io_out8(0xA0, 0x20);
	io_out8(0x20, 0x20); //EOI
}
