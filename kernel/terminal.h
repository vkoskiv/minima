#include "stdint.h"

#pragma once

void terminal_init(int width, int height);
void kprintf(const char *fmt, ...);
void kput(uint8_t byte);
