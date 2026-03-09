#include <kprintf.h>
#include <sched.h>
#include <syscalls.h>
#include <assert.h>
#include <errno.h>
#include <timer.h>

int sys$exit(int retval) {
	current->ret = retval;
	current->state = ts_stopping;
	sched();
	assert(NORETURN);
	return -1;
}

int sys$sleep(int ms) {
	if (ms < 1)
		return -EINVAL;
	sleep(ms);
	return 0;
}

int sys$hello1(int arg0) {
	kprintf("%s[%i] invoked sys$hello1 with: %i\n", current->name, current->id, arg0);
	return 0;
}

int sys$hello2(int arg0, int arg1) {
	kprintf("%s[%i] invoked sys$hello2 with: %i %i\n", current->name, current->id,
	        arg0, arg1);
	return 0;
}

int sys$hello3(int arg0, int arg1, int arg2) {
	kprintf("%s[%i] invoked sys$hello3 with: %i %i %i\n", current->name, current->id,
	        arg0, arg1, arg2);
	return 0;
}

int sys$hello4(int arg0, int arg1, int arg2, int arg3) {
	kprintf("%s[%i] invoked sys$hello4 with: %i %i %i %i\n", current->name, current->id,
	        arg0, arg1, arg2, arg3);
	return 0;
}

int sys$hello5(int arg0, int arg1, int arg2, int arg3, int arg4) {
	kprintf("%s[%i] invoked sys$hello5 with: %i %i %i %i %i\n", current->name, current->id,
	        arg0, arg1, arg2, arg3, arg4);
	return 0;
}

int sys$hello6(int arg0, int arg1, int arg2, int arg3, int arg4, int arg5) {
	kprintf("%s[%i] invoked sys$hello6 with: %i %i %i %i %i %i\n", current->name, current->id,
	        arg0, arg1, arg2, arg3, arg4, arg5);
	return 0;
}

void do_syscall(struct irq_regs regs) {
	struct syscall sc = syscalls[regs.eax];
	if (!sc.handler) {
		kprintf("unknown syscall %i from %s[%i], terminating\n", regs.eax, current->name, current->id);
		current->state = ts_stopping;
		sched();
	}
	int ret;
	switch (sc.args) {
	case 0: ret = ((int (*)())sc.handler)();
		break;
	case 1: ret = ((int (*)(int))sc.handler)(regs.ebx);
		break;
	case 2: ret = ((int (*)(int, int))sc.handler)(regs.ebx, regs.ecx);
		break;
	case 3: ret = ((int (*)(int, int, int))sc.handler)(regs.ebx, regs.ecx, regs.edx);
		break;
	case 4: ret = ((int (*)(int, int, int, int))sc.handler)(regs.ebx, regs.ecx, regs.edx, regs.esi);
		break;
	case 5: ret = ((int (*)(int, int, int, int, int))sc.handler)(regs.ebx, regs.ecx, regs.edx, regs.esi, regs.edi);
		break;
	case 6: ret = ((int (*)(int, int, int, int, int, int))sc.handler)(regs.ebx, regs.ecx, regs.edx, regs.esi, regs.edi, regs.ebp);
		break;
	}
	// FIXME: pass ret in eax to task
	(void)ret;
}

struct syscall syscalls[] = {
	[SYS_EXIT]   = { sys$exit,   1 },
	[SYS_SLEEP]  = { sys$sleep,  1 },
	[SYS_HELLO1] = { sys$hello1, 1 },
	[SYS_HELLO2] = { sys$hello2, 2 },
	[SYS_HELLO3] = { sys$hello3, 3 },
	[SYS_HELLO4] = { sys$hello4, 4 },
	[SYS_HELLO5] = { sys$hello5, 5 },
	[SYS_HELLO6] = { sys$hello6, 6 },
};
