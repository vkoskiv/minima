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
	asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_out16(uint16_t port, uint16_t value) {
	asm volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_out32(uint16_t port, uint32_t value) {
	asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t io_in8(uint16_t port) {
	uint8_t ret;
	asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

static inline uint16_t io_in16(uint16_t port) {
	uint16_t ret;
	asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

static inline uint32_t io_in32(uint16_t port) {
	uint32_t ret;
	asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

// Last resort.
static inline void io_wait(void) {
	asm volatile ("outb %%al, %0x80" : : "a"(0));
}
