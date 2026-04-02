#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <io.h>
#include <serial_debug.h>
#include <panic.h>
#include <x86.h>
#include <debug.h>
#include <kprintf.h>

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

static size_t g_row;
static size_t g_col;
static uint8_t g_cur_color;
static uint16_t *g_buf;

// TODO: We could enable printing in stage0 by keeping this in a global variable
// that then gets bumped up by 0xC0000000 when the initial page mappings are set up.
#define VGAMEM_BASE (VIRT_OFFSET + 0xB8000)

int g_terminal_initialized = 0;

static const uint8_t light_mode = vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_LIGHT_GREY);
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

void terminal_clear(void) {
	g_row = 0;
	g_col = 0;
	g_cur_color = dark_mode;
	for (size_t y = 0; y < TERM_HEIGH; ++y) {
		for (size_t x = 0; x < TERM_WIDTH; ++x) {
			g_buf[y * TERM_WIDTH + x] = vga_entry(' ', g_cur_color);
		}
	}
}

void terminal_init(int width, int height) {
	// TODO: Actually init VGA hardware instead of relying on BIOS state for this.
	TERM_WIDTH = width;
	TERM_HEIGH = height;
	g_buf = (uint16_t *)VGAMEM_BASE;
	terminal_clear();
	g_terminal_initialized = 1;
	kprintf("minima kernel v"VERSION" (c) 2026 Valtteri Koskivuori\n");
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
	cli_push();
	for(size_t y = 0; y < TERM_HEIGH; y++){
		for (size_t x = 0; x < TERM_WIDTH; x++){
			g_buf[y * TERM_WIDTH + x] = y < TERM_HEIGH - 1 ? g_buf[(y + 1) * TERM_WIDTH + x] : ' ' | g_cur_color << 8;
		}
	}
	g_col = 0;
	g_row = TERM_HEIGH - 1;
	cli_pop();
}

static inline void write_serial(char c) {
#if DEBUG_SCHED == 0
	serial_out_byte(c);
#else
	(void)c;
#endif
}

void terminal_putchar(int serial, char c) {
	if (c == '\n') {
		g_col = 0;
		if (++g_row == TERM_HEIGH)
			terminal_scroll();
	} else if (c == '\r') {
		g_col = 0;
	} else if (c == 0x09) {
		for (size_t i = 0; i < TAB_WIDTH; ++i)
			terminal_putchar(serial, ' ');
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
		if (serial)
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
	if (serial)
		write_serial(c);
}
 
void terminal_write(int serial, const char* data, size_t size) {
	for (size_t i = 0; i < size; i++)
		terminal_putchar(serial, data[i]);
}

