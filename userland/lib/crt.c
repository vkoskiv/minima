#include <stddef.h>
#include "../sysroot/usr/include/unistd.h"

int main(int argc, char *argv[]);

int _start(void) {
	exit(main(0, NULL));
	return 0;
}

