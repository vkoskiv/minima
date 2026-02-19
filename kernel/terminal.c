#include <stdbool.h>
#include <stddef.h>
#include "stdint.h"
#include "assert.h"
#include "io.h"
#include "serial_debug.h"
#include "panic.h"
#include "x86.h"
#include "debug.h"

static size_t TERM_WIDTH;
static size_t TERM_HEIGH;
#define TAB_WIDTH 4

/* Hardware text mode color constants. */
enum vga_color {
	VGA_COLOR_BLACK = 0,
	VGA_COLOR_BLUE = 1,
	VGA_COLOR_GREEN = 2,
	VGA_COLOR_CYAN = 3,
	VGA_COLOR_RED = 4,
	VGA_COLOR_MAGENTA = 5,
	VGA_COLOR_BROWN = 6,
	VGA_COLOR_LIGHT_GREY = 7,
	VGA_COLOR_DARK_GREY = 8,
	VGA_COLOR_LIGHT_BLUE = 9,
	VGA_COLOR_LIGHT_GREEN = 10,
	VGA_COLOR_LIGHT_CYAN = 11,
	VGA_COLOR_LIGHT_RED = 12,
	VGA_COLOR_LIGHT_MAGENTA = 13,
	VGA_COLOR_LIGHT_BROWN = 14,
	VGA_COLOR_WHITE = 15,
};

#define vga_entry_color(fg, bg) ((uint8_t)(fg | bg << 4))
#define vga_entry(uc, color) (((uint16_t)uc & 0xff) | (uint16_t)color << 8)

size_t strlen(const char *str) {
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}

static size_t g_row;
static size_t g_col;
static uint8_t g_cur_color;
static uint16_t *g_buf;

// TODO: We could enable printing in stage0 by keeping this in a global variable
// that then gets bumped up by 0xC0000000 when the initial page mappings are set up.
#define VGAMEM_BASE (VIRT_OFFSET + 0xB8000)

int g_terminal_initialized = 0;

static const uint8_t light_mode = vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_WHITE);
static const uint8_t dark_mode = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

static void terminal_redraw_color(void) {
	for(size_t y = 0; y < TERM_HEIGH; y++){
		for (size_t x = 0; x < TERM_WIDTH; x++){
			uint16_t *cell = &g_buf[y * TERM_WIDTH + x];
			*cell = vga_entry(*cell, g_cur_color);
		}
	}
}

void toggle_dark_mode(void) {
	if (g_cur_color == light_mode)
		g_cur_color = dark_mode;
	else
		g_cur_color = light_mode;
	terminal_redraw_color();
}

void terminal_init(int width, int height) {
	// TODO: Actually init VGA hardware instead of relying on BIOS state for this.
	TERM_WIDTH = width;
	TERM_HEIGH = height;
	g_row = 0;
	g_col = 0;
	g_cur_color = dark_mode;
	g_buf = (uint16_t *)VGAMEM_BASE;
	for (size_t y = 0; y < TERM_HEIGH; ++y) {
		for (size_t x = 0; x < TERM_WIDTH; ++x) {
			g_buf[y * TERM_WIDTH + x] = vga_entry(' ', g_cur_color);
		}
	}
	toggle_color();
	g_terminal_initialized = 1;
}
 
static void set_cursor_pos(int x, int y) {
	uint16_t pos = y * TERM_WIDTH + x;
	io_out8(0x3D4, 0x0F);
	io_out8(0x3D5, (uint8_t) (pos & 0xFF));
	io_out8(0x3D4, 0x0E);
	io_out8(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

static void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
	assert(x < TERM_WIDTH);
	assert(y < TERM_HEIGH);
	g_buf[y * TERM_WIDTH + x] = vga_entry(c, color);
}

// TODO: Fix this stuff up to be fully re-entrant instead of slapping in critical regions
// Or better yet, turn this into a character device that kprintf() write()s to and sync stuff
// that way
static void terminal_scroll() {
	cli();
	for(size_t y = 0; y < TERM_HEIGH; y++){
		for (size_t x = 0; x < TERM_WIDTH; x++){
			g_buf[y * TERM_WIDTH + x] = y < TERM_HEIGH - 1 ? g_buf[(y + 1) * TERM_WIDTH + x] : ' ' | g_cur_color << 8;
		}
	}
	g_col = 0;
	g_row = TERM_HEIGH - 1;
	sti();
}

static inline void write_serial(char c) {
#if DEBUG_SCHED == 0
	serial_out_byte(c);
#else
	(void)c;
#endif
}
void terminal_putchar(char c) {
	if (c == '\n') {
		g_col = 0;
		if (++g_row == TERM_HEIGH)
			terminal_scroll();
	} else if (c == '\r') {
		g_col = 0;
	} else if (c == 0x09) {
		for (size_t i = 0; i < TAB_WIDTH; ++i)
			terminal_putchar(' ');
	} else if (c == 0x08) {
		if (!g_col) {
			if (!g_row)
				return;
			g_row--;
			g_col = TERM_WIDTH - 1;
		} else {
			--g_col;
			terminal_putentryat(' ', g_cur_color, g_col, g_row);
		}
		write_serial(c);
	} else {
		terminal_putentryat(c, g_cur_color, g_col, g_row);
		if (++g_col == TERM_WIDTH) {
			g_col = 0;
			if (++g_row == TERM_HEIGH) {
				terminal_scroll();
			}
		}
	}
	set_cursor_pos(g_col, g_row);
	write_serial(c);
}
 
void terminal_write(const char* data, size_t size) {
	for (size_t i = 0; i < size; i++)
		terminal_putchar(data[i]);
}
 
void kput(uint8_t byte) {
	if (!g_terminal_initialized)
		panic("");
	terminal_putchar(byte);
}

static int places(int n) {
	int places = 1;
	while (n >= 10) {
		places++;
		n /= 10;
	}
	return places;
}

static void pru32(uint32_t num) {
	//We've got to marshal this number here to build up a string.
	//No floating point yet. And probably not for a while!
	uint8_t len = places(num);
	char buf[len];

	for (int i = 0; i < len; ++i) {
		uint32_t remainder = num % 10;
		buf[len - i - 1] = remainder + '0';
		num /= 10;
	}
	terminal_write(buf, len);
}

static void pri32(int32_t num) {
	if (num < 0) {
		terminal_putchar('-');
		num = -num;
	}
	pru32(num);
}

static void kprinthex_internal(uint8_t byte) {
	static const char *hexchars = "0123456789ABCDEF";
	char chars[2];
	uint8_t remainder = byte % 16;
	chars[0] = hexchars[remainder];
	byte /= 16;
	remainder = byte % 16;
	chars[1] = hexchars[remainder];
	kput(chars[1]);
	kput(chars[0]);
}

static void kprintaddr32(void *addr) {
	terminal_write("0x", 2);
	uint32_t val = (uint32_t)addr;
	kprinthex_internal(val >> 24);
	kprinthex_internal(val >> 16);
	kprinthex_internal(val >> 8);
	kprinthex_internal(val >> 0);
}

// This is dumb, incomplete and fragile, but it'll get us going for now.
// I just made it now so I don't have to write out annoying separate kprint()
// calls for everything.
// Eventually we want a proper state machine in here to process all the printf
// formatting specifiers, but today is not the day for that.
void kprintf_internal(const char *fmt, va_list vl) {
	if (!g_terminal_initialized)
		panic("");
	if (!fmt)
		return;
	for (size_t i = 0; fmt[i]; ++i) {
		if (fmt[i] == '%') {
			switch (fmt[i + 1]) {
				case 'h': { // Hex
					i += 2;
					kprintaddr32((void *)va_arg(vl, uint32_t));
				} break;
				case 'u': {
					i += 2;
					pru32((uint32_t)va_arg(vl, uint32_t));
				} break;
				case 'i': {
					i += 2;
					pri32((int32_t)va_arg(vl, int32_t));
				} break;
				case 's': { // string
					i += 2;
					char *str = va_arg(vl, char *);
					size_t str_len = strlen(str);
					terminal_write(str, str_len);
				} break;
				case 'c': { // char
					i += 2;
					char c = va_arg(vl, int);
					terminal_putchar(c);
				} break;
				default:
					continue;
					break;
			}
		}
		terminal_putchar(fmt[i]);
	}
}

void kprintf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	kprintf_internal(fmt, args);
	va_end(args);
}
