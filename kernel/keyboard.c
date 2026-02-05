//
//  keyboard.c
//  xcode
//
//  Created by Valtteri Koskivuori on 25/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "keyboard.h"
#include "terminal.h"
#include <stddef.h>

struct scancode {
	uint8_t scancode;
	uint8_t byte;
};

static const struct scancode codes[] = {
	{0x1E, 'a'},
	{0x30, 'b'},
	{0x2E, 'c'},
	{0x20, 'd'},
	{0x12, 'e'},
	{0x21, 'f'},
	{0x22, 'g'},
	{0x23, 'h'},
	{0x17, 'i'},
	{0x24, 'j'},
	{0x25, 'k'},
	{0x26, 'l'},
	{0x32, 'm'},
	{0x31, 'n'},
	{0x18, 'o'},
	{0x19, 'p'},
	{0x10, 'q'},
	{0x13, 'r'},
	{0x1F, 's'},
	{0x14, 't'},
	{0x16, 'u'},
	{0x2F, 'v'},
	{0x11, 'w'},
	{0x2D, 'x'},
	{0x15, 'y'},
	{0x2C, 'z'},
	{0x39, ' '},
	{0x02, '1'},
	{0x03, '2'},
	{0x04, '3'},
	{0x05, '4'},
	{0x06, '5'},
	{0x07, '6'},
	{0x08, '7'},
	{0x09, '8'},
	{0x0A, '9'},
	{0x0B, '0'},
	{0x0E, 0x08}, // Backspace
	{0x0F, 0x09}, // Horizontal tab
	{0x1C, '\n'},  // Return
	{0x01, 0x1B}, // ESC
	{0x2B, '\''}, // Apostrophe
	{0x33, ','},
	{0x34, '.'},
	{0x35, '-'},
	{0x28, '\''},
};

static const struct scancode shifted_codes[] = {
	{0x1E, 'A'},
	{0x30, 'B'},
	{0x2E, 'C'},
	{0x20, 'D'},
	{0x12, 'E'},
	{0x21, 'F'},
	{0x22, 'G'},
	{0x23, 'H'},
	{0x17, 'I'},
	{0x24, 'J'},
	{0x25, 'K'},
	{0x26, 'L'},
	{0x32, 'M'},
	{0x31, 'N'},
	{0x18, 'O'},
	{0x19, 'P'},
	{0x10, 'Q'},
	{0x13, 'R'},
	{0x1F, 'S'},
	{0x14, 'T'},
	{0x16, 'U'},
	{0x2F, 'V'},
	{0x11, 'W'},
	{0x2D, 'X'},
	{0x15, 'Y'},
	{0x2C, 'Z'},
	{0x39, ' '},
	{0x02, '!'},
	{0x03, '"'},
	{0x04, '#'},
	{0x05, '$'},
	{0x06, '%'},
	{0x07, '&'},
	{0x08, '/'},
	{0x09, '('},
	{0x0A, ')'},
	{0x0B, '='},
	{0x0E, 0x08}, // Backspace
	{0x0F, 0x09}, // Horizontal tab
	{0x1C, '\n'},  // Return
	{0x01, 0x1B}, // ESC
	{0x2B, '\''}, // Apostrophe
	{0x33, ';'},
	{0x34, ':'},
	{0x35, '_'},
	{0x28, '\''},
};

#define SCANCODE_COUNT (sizeof(codes) / sizeof(struct scancode))

#define RB_CAP 256
struct ringbuf {
	char buf[RB_CAP];
	uint32_t head, tail;
};

static struct ringbuf s_rb = { 0 };

static int rb_write(char scancode) {
	uint32_t avail = (s_rb.tail - s_rb.head - 1) & (RB_CAP - 1);
	if (!avail)
		return 1;
	s_rb.buf[(s_rb.head + 1) & (RB_CAP - 1)] = scancode;
	s_rb.head = (s_rb.head + 1) & (RB_CAP - 1);
	return 0;
}

static int rb_read(char *out) {
	uint32_t avail = (s_rb.head - s_rb.tail) & (RB_CAP - 1);
	if (!avail)
		return 1;
	*out = s_rb.buf[(s_rb.tail + 1) & (RB_CAP - 1)];
	s_rb.tail = (s_rb.tail + 1) & (RB_CAP - 1);
	return 0;
}

static int kbd_read(char *buf, size_t n) {
	size_t idx = 0;
	while (idx < n) {
		char c;
		int ret = rb_read(&c);
		if (ret)
			return idx;
		buf[idx++] = c;
	}
	return idx;
}

struct char_dev chardev_kbd = {
	.read = kbd_read,
	.write = NULL,
};

static int g_shifted;

void kbd_init(void) {
	g_shifted = 0;
}

uint8_t lowercase(uint8_t byte) {
	if (byte > 64 && byte < 91) { // A-Z ASCII
		byte += 32; // Offset to lowercase
	}
	return byte;
}

void received_scancode(uint8_t scancode) {
	// uint8_t down = !(scancode & 0x80);
	uint8_t lowerSeven = scancode & 0x7f;
	
	// Update shifted status
	// For some reason left shift behaves completely differently from right shift.
	// so 0x2A is left shift down, and 0xAA is left shift up.
	if (lowerSeven == 0x36 || scancode == 0x2A || scancode == 0xAA) {
		g_shifted = !g_shifted;
		return;
	} else {
		// Ignore keyup for now, except for shift
		if (scancode & 0x80) return;
	}
	
	uint8_t byte = 0xFF;
	
	const struct scancode *list = g_shifted ? shifted_codes : codes;
	for (uint32_t i = 0; i < SCANCODE_COUNT; ++i) {
		if (list[i].scancode == scancode) {
			byte = list[i].byte;
		}
	}

	if (byte != 0xFF) {
		int ret = rb_write(byte);
		if (ret)
			kput('!');
	} else {
		kprintf("?(");kprinthex(scancode);kput(')');
	}
}
