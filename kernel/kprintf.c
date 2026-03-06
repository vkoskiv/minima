#include <terminal.h>
#include <kprintf.h>
#include <stddef.h>
#include <utils.h>
#include <panic.h>
#include <assert.h>

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

static int is_num(char c) {
	return c >= '0' && c <= '9';
}

uint32_t try_get_num(const char *str, uint32_t *num_out) {
	uint32_t places[11];
	size_t idx = 0;
	while (is_num(str[idx])) {
		for (size_t i = 0; i < 11; ++i)
			places[i] *= 10;
		places[idx] = str[idx] - '0';
		idx++;
	}
	if (!idx)
		return idx;
	uint32_t prev_sum = 0;
	uint32_t sum = 0;
	for (size_t i = 0; i < idx; ++i) {
		prev_sum = sum;
		sum += places[i];
		assert(sum >= prev_sum);
	}
	*num_out = sum;
	return idx;
}

static void kprinthex4(uint32_t val) {
	terminal_write("0x", 2);
	kprinthex_internal(val >> 24);
	kprinthex_internal(val >> 16);
	kprinthex_internal(val >> 8);
	kprinthex_internal(val >> 0);
}

static void kprinthex3(uint32_t val) {
	terminal_write("0x", 2);
	kprinthex_internal(val >> 16);
	kprinthex_internal(val >> 8);
	kprinthex_internal(val >> 0);
}

static void kprinthex2(uint16_t val) {
	terminal_write("0x", 2);
	kprinthex_internal(val >> 8);
	kprinthex_internal(val >> 0);
}

static void kprinthex1(uint8_t val) {
	terminal_write("0x", 2);
	kprinthex_internal(val);
}

static void kprinthex1_noprefix(uint8_t val) {
	kprinthex_internal(val);
}

void kprintf_internal(const char *fmt, va_list vl) {
	if (!g_terminal_initialized)
		panic("");
	if (!fmt)
		return;
	for (size_t i = 0; fmt[i]; ++i) {
		if (fmt[i] == '%') {
			uint32_t num = 0;
			uint8_t num_chars = try_get_num(&fmt[i + 1], &num);
			i += num_chars;
			switch (fmt[i + 1]) {
				case '%': {
					terminal_putchar('%');
					i += 2;
				} break;
				case 'h': { // Hex
					i += 2;
					if (!num_chars)
						kprinthex4((uint32_t)va_arg(vl, uint32_t));
					else if (!num)
						kprinthex1_noprefix((uint8_t)va_arg(vl, uint32_t));
					else if (num <= 1)
						kprinthex1((uint8_t)va_arg(vl, uint32_t));
					else if (num <= 2)
						kprinthex2((uint16_t)va_arg(vl, uint32_t));
					else if (num <= 3)
						kprinthex3((uint32_t)va_arg(vl, uint32_t));
					else
						kprinthex4((uint32_t)va_arg(vl, uint32_t));
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
					if (!str) {
						terminal_write("(null)", 6);
					} else {
						size_t str_len = strlen(str);
						if (num_chars)
							str_len = min(str_len, num);
						terminal_write(str, str_len);
					}
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
			if (fmt[i] == 0)
				return;
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
