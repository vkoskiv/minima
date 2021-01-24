#include "stdint.h"

void terminal_init(int width, int height);
void kprint(const char* data);
void kprintnum(uint64_t num);
void kprintf(const char *fmt, ...);
