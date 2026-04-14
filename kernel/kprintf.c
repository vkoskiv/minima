#include <terminal.h>
#include <kprintf.h>
#include <stddef.h>
#include <utils.h>
#include <panic.h>
#include <assert.h>

static void do_kput(int serial, uint8_t byte) {
	if (!g_terminal_initialized)
		panic("");
	terminal_putchar(serial, byte);
}

void kput(uint8_t byte) {
	do_kput(1, byte);
}

void kput_noserial(uint8_t byte) {
	do_kput(0, byte);
}

static size_t places(uint32_t n) {
	size_t places = 1;
	while (n >= 10) {
		places++;
		n /= 10;
	}
	return places;
}

static void pru32(int serial, uint32_t num) {
	//We've got to marshal this number here to build up a string.
	//No floating point yet. And probably not for a while!
	uint8_t len = places(num);
	char buf[len];

	for (int i = 0; i < len; ++i) {
		uint32_t remainder = num % 10;
		buf[len - i - 1] = remainder + '0';
		num /= 10;
	}
	terminal_write(serial, buf, len);
}

static void pri32(int serial, int32_t num) {
	if (num < 0) {
		terminal_putchar(serial, '-');
		num = -num;
	}
	pru32(serial, num);
}

static void kprinthex_internal(int serial, uint8_t byte) {
	static const char *hexchars = "0123456789ABCDEF";
	char chars[2];
	uint8_t remainder = byte % 16;
	chars[0] = hexchars[remainder];
	byte /= 16;
	remainder = byte % 16;
	chars[1] = hexchars[remainder];
	do_kput(serial, chars[1]);
	do_kput(serial, chars[0]);
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

static void kprinthex4(int serial, uint32_t val) {
	terminal_write(serial, "0x", 2);
	kprinthex_internal(serial, val >> 24);
	kprinthex_internal(serial, val >> 16);
	kprinthex_internal(serial, val >> 8);
	kprinthex_internal(serial, val >> 0);
}

static void kprinthex3(int serial, uint32_t val) {
	terminal_write(serial, "0x", 2);
	kprinthex_internal(serial, val >> 16);
	kprinthex_internal(serial, val >> 8);
	kprinthex_internal(serial, val >> 0);
}

static void kprinthex2(int serial, uint16_t val) {
	terminal_write(serial, "0x", 2);
	kprinthex_internal(serial, val >> 8);
	kprinthex_internal(serial, val >> 0);
}

static void kprinthex1(int serial, uint8_t val) {
	terminal_write(serial, "0x", 2);
	kprinthex_internal(serial, val);
}

static void kprinthex1_noprefix(int serial, uint8_t val) {
	kprinthex_internal(serial, val);
}

void do_kprintf_internal(int serial, const char *fmt, va_list vl) {
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
				case '%':
					terminal_putchar(serial, '%');
					break;
				case 'h': {
					if (!num_chars)
						kprinthex4(serial, (uint32_t)va_arg(vl, uint32_t));
					else if (!num)
						kprinthex1_noprefix(serial, (uint8_t)va_arg(vl, uint32_t));
					else if (num <= 1)
						kprinthex1(serial, (uint8_t)va_arg(vl, uint32_t));
					else if (num <= 2)
						kprinthex2(serial, (uint16_t)va_arg(vl, uint32_t));
					else if (num <= 3)
						kprinthex3(serial, (uint32_t)va_arg(vl, uint32_t));
					else
						kprinthex4(serial, (uint32_t)va_arg(vl, uint32_t));
				} break;
				case 'u':
					pru32(serial, (uint32_t)va_arg(vl, uint32_t));
					break;
				case 'i':
					pri32(serial, (int32_t)va_arg(vl, int32_t));
					break;
				case 's': {
					char *str = va_arg(vl, char *);
					if (!str) {
						terminal_write(serial, "(null)", 6);
					} else {
						// FIXME: Flip this? use strlen if no num_chars?
						size_t str_len = strlen(str);
						if (num_chars)
							str_len = min(str_len, num);
						terminal_write(serial, str, str_len);
					}
				} break;
				case 'c':
					terminal_putchar(serial, va_arg(vl, int));
				break;
				default:
					terminal_putchar(serial, fmt[i]);
					continue;
					break;
			}
			i++;
		} else {
			terminal_putchar(serial, fmt[i]);
		}
	}
}

void kprintf_internal(const char *fmt, va_list vl) {
	do_kprintf_internal(1, fmt, vl);
}

void kprintf_internal_noserial(const char *fmt, va_list vl) {
	do_kprintf_internal(0, fmt, vl);
}

void kprintf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	kprintf_internal(fmt, args);
	va_end(args);
}

void kprintf_noserial(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	kprintf_internal_noserial(fmt, args);
	va_end(args);
}
