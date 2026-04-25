#ifndef _TERMINAL_H_
#define _TERMINAL_H_
#include <stdint.h>
#include <stddef.h>

extern int g_terminal_initialized;
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

void terminal_init(int width, int height);
void terminal_register_dev(void);

// FIXME: Eventually remove these
void terminal_putchar(int serial, char c);
void terminal_write(int serial, const char* data, size_t size);
void toggle_dark_mode(void);
void terminal_clear(void);

#endif
