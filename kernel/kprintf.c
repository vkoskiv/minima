#include <terminal.h>
#include <kprintf.h>
#include <stddef.h>
#include <utils.h>
#include <panic.h>

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
