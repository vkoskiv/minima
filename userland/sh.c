#include "lib.h"

#define LINE_MAX 4096

static char getchar(void) {
	char c;
	int ret = read(0, &c, 1);
	return c;
}

// TODO: Once userland is shaping up, implement devfs and then
// turn terminal.c into a character special device in /dev/tty.
// Then move this code into the line discipline.
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

int _start(void) {
	char cmd[512];
	for (;;) {
		write(1, "> ", 2);
		int bytes = getline(cmd, 512);
		cmd[bytes] = 0;
		// TODO: fork & execute
		// for now, just echo cmd
		write(1, "\n", 1);
		write(1, cmd, bytes);
		write(1, "\n", 1);
	}
}
