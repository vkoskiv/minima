#include "../../../kernel/include/user/syscalls.h"

int read(int fd, void *buf, unsigned count) {
	return syscall3(SYS_READ, fd, buf, count);
}

int write(int fd, const void *buf, unsigned count) {
	return syscall3(SYS_WRITE, fd, buf, count);
}

void exit(int ret) {
	syscall1(SYS_EXIT, ret);
}

