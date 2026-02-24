#ifndef _TERMINAL_H_
#define _TERMINAL_H_
#include <stdint.h>
#include <stddef.h>

extern int g_terminal_initialized;
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

void terminal_init(int width, int height);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void toggle_dark_mode(void);
void terminal_clear(void);

#endif
