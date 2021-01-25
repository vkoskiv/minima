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
	asm volatile ("io_out8 %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t io_in8(uint16_t port) {
	uint8_t ret;
	asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}
