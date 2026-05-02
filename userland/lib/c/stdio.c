// #include <stdio.h>
#include "../../../kernel/include/user/syscalls.h"

char getchar(void) {
	char c;
	int ret = read(0, &c, 1);
	return c;
}

int getline(char *out, unsigned n) {
	unsigned bytes = 0;
	char c;
	while (bytes < n && (c = getchar()) != '\n') {
		if (c == 0x08) { // backspace
			write(1, &c, 1);
			out[--bytes] = 0;
		} else {
			write(1, &c, 1);
			out[bytes++] = c;
		}
	}
	out[bytes] = 0;
	return bytes;
}

