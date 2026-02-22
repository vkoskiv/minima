#include "terminal.h"
#include "sched.h"
#include "syscalls.h"
#include "assert.h"

int sys$exit(int retval) {
	current->ret = retval;
	current->stopping = 1;
	sched();
	assert(NORETURN);
	return -1;
}

int sys$hello6(int arg1, int arg2, int arg3, int arg4, int arg5, int arg6) {
	kprintf("%s[%i] invoked sys$hello with:\n\t%i %i %i %i %i %i\n", current->name, current->id,
	        arg1, arg2, arg3, arg4, arg5, arg6);
	return 0;
}

int sys$hello1(int arg) {
	kprintf("%s[%i] invoked sys$hello1 with: %i\n", current->name, current->id, arg);
	return 0;
}

void do_syscall(struct irq_regs regs) {
	struct syscall sc = syscalls[regs.eax];
	if (!sc.handler) {
		kprintf("unknown syscall %i from %s[%i], terminating\n", regs.eax, current->name, current->id);
		current->stopping = 1;
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
	[0]  = { sys$exit,   1 },
	[42] = { sys$hello1, 1 },
	[43] = { sys$hello6, 6 },
};
