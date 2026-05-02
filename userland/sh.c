// Too tired to convince GCC to actually care about -I flags I've specified, so
// I'll have to use stupid absolute paths instead.
#include "sysroot/usr/include/stdio.h"

#define LINE_MAX 4096

int main(int argc, char *argv[]) {
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
		if (cmd[0] == '\x1B')
			break;
	}
	return 0;
}
