#ifndef _KPRINTF_H_
#define _KPRINTF_H_

#include <stdarg.h>
#include <stdint.h>

#define PLURAL(x) (x) > 1 ? "s" : (x) == 0 ? "s" : ""

void kprintf(const char *fmt, ...);
void kprintf_noserial(const char *fmt, ...);
void kprintf_internal(const char *fmt, va_list vl);
void kprintf_internal_noserial(const char *fmt, va_list vl);
void kput(uint8_t byte);
void kput_noserial(uint8_t byte);

#endif
