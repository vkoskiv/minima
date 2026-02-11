#include <stdbool.h>
#include <stddef.h>
#include "stdint.h"
#include "assert.h"
#include "io.h"
#include "serial_debug.h"
#include <stdarg.h>
#include "panic.h"

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

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
	return fg | bg << 4;
}
 
static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
	return (uint16_t) uc | (uint16_t) color << 8;
}

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
#define VGAMEM_BASE 0xC00B8000

int g_terminal_initialized = 0;

void terminal_init(int width, int height) {
	// TODO: Actually init VGA hardware instead of relying on BIOS state for this.
	TERM_WIDTH = width;
	TERM_HEIGH = height;
	g_row = 0;
	g_col = 0;
	g_cur_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
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

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
	ASSERT(x < TERM_WIDTH);
	ASSERT(y < TERM_HEIGH);
	g_buf[y * TERM_WIDTH + x] = vga_entry(c, color);
}

void terminal_scroll() {
	for(size_t y = 0; y < TERM_HEIGH; y++){
		for (size_t x = 0; x < TERM_WIDTH; x++){
			g_buf[y * TERM_WIDTH + x] = y < TERM_HEIGH - 1 ? g_buf[(y + 1) * TERM_WIDTH + x] : ' ';
		}
	}
}

void terminal_putchar(char c) {
	if (c == '\n') {
		if (++g_row == TERM_HEIGH) {
			terminal_scroll();
			g_row = TERM_HEIGH - 1;
		}
		g_col = 0;
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
		serial_out_byte(c);
	} else {
		terminal_putentryat(c, g_cur_color, g_col, g_row);
		if (++g_col == TERM_WIDTH) {
			g_col = 0;
			if (++g_row == TERM_HEIGH) {
				terminal_scroll();
				g_row = TERM_HEIGH - 1;
			}
		}
	}
	set_cursor_pos(g_col, g_row);
	serial_out_byte(c);
}
 
void terminal_write(const char* data, size_t size) {
	for (size_t i = 0; i < size; i++)
		terminal_putchar(data[i]);
}
 
void kprint(const char *data) {
	terminal_write(data, strlen(data));
}

void kput(uint8_t byte) {
	if (!g_terminal_initialized)
		panic();
	terminal_putchar(byte);
}

int places(uint32_t n) {
	if (n < 10) return 1;
	return 1 + places(n / 10);
}

void kprintnum(uint32_t num) {
	//We've got to marshal this number here to build up a string.
	//No floating point yet. And probably not for a while!
	uint8_t len = places(num);
	char buf[len + 1];

	for (int i = 0; i < len; ++i) {
		uint32_t remainder = num % 10;
		buf[len - i - 1] = remainder + '0';
		num /= 10;
	}
	buf[len] = '\0';
	kprint(buf);
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

void kprinthex(uint8_t byte) {
	if (!g_terminal_initialized)
		panic();
	kprint("0x");
	kprinthex_internal(byte);
}

void kprintaddr32(void *addr) {
	kprint("0x");
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
void kprintf(const char *fmt, ...) {
	if (!g_terminal_initialized)
		panic();
	if (!fmt)
		return;
	size_t len = strlen(fmt);
	va_list vl;
	va_start(vl, fmt);
	for (size_t i = 0; i < len; ++i) {
		if (fmt[i] == '%') {
			// I just completely made these up, will fix later I guess
			switch (fmt[i + 1]) {
				case 'h': { // Hex
					i += 2;
					kprintaddr32((void *)va_arg(vl, uint32_t));
				} break;
				// FIXME: need to handle sign at some point
				case 'i': { // Int
					i += 2;
					kprintnum((uint32_t)va_arg(vl, uint32_t));
				} break;
				case 's': { // string
					i += 2;
					char *str = va_arg(vl, char *);
					size_t str_len = strlen(str);
					terminal_write(str, str_len);
				} break;
				default:
					continue;
					break;
			}
		}
		if (i < len) terminal_putchar(fmt[i]);
	}
	va_end(vl);
}
