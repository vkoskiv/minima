#include "stdint.h"

#pragma once

#define PLURAL(x) (x) > 1 ? "s" : (x) == 0 ? "s" : ""

extern int g_terminal_initialized;

void terminal_init(int width, int height);
void kprintf(const char *fmt, ...);
void kput(uint8_t byte);
void kprinthex(uint8_t byte); // for keyboard.c
