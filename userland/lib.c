#include "lib.h"
#include <user/syscalls.h>


int read(int fd, void *buf, unsigned count) {
	return syscall3(SYS_READ, fd, buf, count);
}

int write(int fd, const void *buf, unsigned count) {
	return syscall3(SYS_WRITE, fd, buf, count);
}
