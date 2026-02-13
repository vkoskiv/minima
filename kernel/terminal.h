#include "stdint.h"
#include <stdarg.h>

#pragma once

#define PLURAL(x) (x) > 1 ? "s" : (x) == 0 ? "s" : ""

extern int g_terminal_initialized;

void terminal_init(int width, int height);
void kprintf(const char *fmt, ...);
void kprintf_internal(const char *fmt, va_list vl);
void kput(uint8_t byte);
void toggle_dark_mode(void);
