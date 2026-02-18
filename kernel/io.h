//
//  io.h
//  xcode
//
//  Created by Valtteri on 24.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include "stdint.h"

static inline void io_out8(uint16_t port, uint8_t value) {
	asm volatile(
		"outb %[port], %[value]"
		: /* No outputs */
		: [value]"a"(value), [port]"Nd"(port)
		: /* No clobbers */
	);
}

static inline void io_out16(uint16_t port, uint16_t value) {
	asm volatile(
		"outw %[port], %[value]"
		: /* No outputs */
		: [value]"a"(value), [port]"Nd"(port)
		: /* No clobbers */
	);
}

static inline void io_out32(uint16_t port, uint32_t value) {
	asm volatile(
		"outl %[port], %[value]"
		: /* No outputs */
		: [value]"a"(value), [port]"Nd"(port)
		: /* No clobbers */
	);
}

static inline uint8_t io_in8(uint16_t port) {
	uint8_t ret;
	asm volatile(
		"inb %[ret], %[port]"
		: [ret]"=a"(ret)
		: [port]"Nd"(port)
		: /* No clobbers */
	);
	return ret;
}

static inline uint16_t io_in16(uint16_t port) {
	uint16_t ret;
	asm volatile(
		"inw %[ret], %[port]"
		: [ret]"=a"(ret)
		: [port]"Nd"(port)
		: /* No clobbers */
	);
	return ret;
}

static inline uint32_t io_in32(uint16_t port) {
	uint32_t ret;
	asm volatile(
		"inl %[ret], %[port]"
		: [ret]"=a"(ret)
		: [port]"Nd"(port)
		: /* No clobbers */
	);
	return ret;
}

// Last resort.
static inline void io_wait(void) {
	io_out8(0x80, 0);
}
