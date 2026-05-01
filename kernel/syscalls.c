#include <kprintf.h>
#include <sched.h>
#include <user/syscalls.h>
#include <assert.h>
#include <errno.h>
#include <timer.h>
#include <fs/vfs.h>

int sys$exit(int retval) {
	current->ret = retval;
	task_kill(current);
	assert(NORETURN);
	return -1;
}

int sys$sleep(int ms) {
	if (ms < 1)
		return -EINVAL;
	sleep(ms);
	return 0;
}

int sys$read(int fd, char *buf, size_t count) {
	struct vfs_file *f = current->files[fd];
	if (!f)
		return -EBADF;
	return vfs_read(f, buf, count);
}

int sys$write(int fd, char *buf, size_t count) {
	struct vfs_file *f = current->files[fd];
	if (!f)
		return -EBADF;
	return vfs_write(f, buf, count);
}

int do_syscall(const struct irq_regs *const regs) {
	struct syscall sc = syscalls[regs->eax];
	if (!sc.handler) {
		kprintf("unknown syscall %i from %s[%i], terminating\n", regs->eax, current->name, current->id);
		current->state = ts_stopping;
		sched();
	}
	switch (sc.args) {
	case 0: return ((int (*)())sc.handler)();
	case 1: return ((int (*)(int))sc.handler)(regs->ebx);
	case 2: return ((int (*)(int, int))sc.handler)(regs->ebx, regs->ecx);
	case 3: return ((int (*)(int, int, int))sc.handler)(regs->ebx, regs->ecx, regs->edx);
	case 4: return ((int (*)(int, int, int, int))sc.handler)(regs->ebx, regs->ecx, regs->edx, regs->esi);
	case 5: return ((int (*)(int, int, int, int, int))sc.handler)(regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
	case 6: return ((int (*)(int, int, int, int, int, int))sc.handler)(regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi, regs->ebp);
	}
	return 0;
}

struct syscall syscalls[] = {
	[SYS_EXIT]   = { sys$exit,   1 },
	[SYS_SLEEP]  = { sys$sleep,  1 },
	[SYS_READ]   = { sys$read,   3 },
	[SYS_WRITE]  = { sys$write,  3 },
};
