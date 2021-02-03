//
//  serial_debug.c
//  xcode
//
//  Created by Valtteri Koskivuori on 03/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "serial_debug.h"
#include "io.h"
#include <stdbool.h>

static bool serial_ready = false;
static bool color_enabled = false;

// 0x3F8 == COM1

void prepare_serial_device(void) {
	// Setup serial at 38400 baud
	io_out8(0x3F8 + 1, 0x00);
	io_out8(0x3F8 + 3, 0x80);
	io_out8(0x3F8 + 0, 0x02);
	io_out8(0x3F8 + 1, 0x00);
	io_out8(0x3F8 + 3, 0x03);
	io_out8(0x3F8 + 2, 0xC7);
	io_out8(0x3F8 + 4, 0x0B);
}

void serial_out_byte(char c) {
	if (!serial_ready) {
		prepare_serial_device();
		serial_ready = true;
	}
	// Wait for it to free up
	while ((io_in8(0x3F8 + 5) % 20) == 0);
	// Send it!
	io_out8(0xE9, c);
}

void toggle_color(void) {
	io_out8(0xE9, 0x1B);
	serial_out_byte('[');
	if (color_enabled) {
		// Default color
		serial_out_byte('0');
	} else {
		// Blue
		serial_out_byte('3');
		serial_out_byte('4');
	}
	serial_out_byte('m');
	color_enabled = !color_enabled;
}
