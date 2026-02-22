//
//  serial_debug.c
//
//  Created by Valtteri Koskivuori on 03/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include <serial_debug.h>
#include <io.h>
#include <panic.h>

static int s_color_enabled = 0;
static int s_serial_found = 0;
static int s_emu_serial_found = 0;

// 0x3F8 == COM1
#define PORT 0x3F8

static void prepare_serial_device(void) {
	// Check for port 0xE9 hack, which is supported in QEMU and Bochs
	uint8_t test = io_in8(0xE9);
	if (test == 0xE9)
		s_emu_serial_found = 1;
	// Setup serial at 38400 baud
	// Disable interrupts
	io_out8(PORT + 1, 0x00);
	// Set DLAB bit to select baud rate
	io_out8(PORT + 3, 0x80);
	// Send 0x0003 as the divisor, (115200 / 3 = 38400)
	io_out8(PORT + 0, 0x03);
	io_out8(PORT + 1, 0x00);
	// 8N1
	io_out8(PORT + 3, 0x03);
	// Enable FIFO
	io_out8(PORT + 2, 0xC7);
	io_out8(PORT + 4, 0x0B); // Enable IRQ, set RTS and DSR

	// Now test loopback
	io_out8(PORT + 4, 0x1E); // Enable loopback
	io_out8(PORT + 0, 0xAE); // Send 0xAE
	if (io_in8(PORT + 0) == 0xAE) // Got 0xAE back?
		s_serial_found = 1;
	io_out8(PORT + 4, 0x0F); // Set to operating mode
}

void serial_setup(void) {
	prepare_serial_device();
}

void serial_out_byte(char c) {
	// Send to emulator output, if available
	if (s_emu_serial_found)
		io_out8(0xE9, c);

	if (!s_serial_found)
		return;
	// Wait for it to free up
	while ((io_in8(PORT + 5) % 20) == 0);
	// Send it!
	io_out8(PORT + 0, c);
}

void toggle_color(void) {
	if (!s_emu_serial_found)
		return;
	io_out8(0xE9, 0x1B);
	serial_out_byte('[');
	if (s_color_enabled) {
		// Default color
		serial_out_byte('0');
	} else {
		// Blue
		serial_out_byte('3');
		serial_out_byte('4');
	}
	serial_out_byte('m');
	s_color_enabled = !s_color_enabled;
}
