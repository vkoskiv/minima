#ifndef _SYSCALLS_H_
#define _SYSCALLS_H_

#include <stdint.h>
#include <idt.h>

struct syscall {
	void *handler;
	uint8_t args;
};

extern struct syscall syscalls[];
int do_syscall(const struct irq_regs *const regs);

static inline int syscall0(int num) {
	int ret;
	asm volatile(
		"mov eax, %[scnum];"
		"int 0x80;"
		: "+a"(ret)
		: [scnum]"a"(num)
		: /* No clobbers */
	);
	return ret;
}

static inline int syscall1(int num, int a0) {
	asm volatile(
		"mov eax, %[scnum];"
		"mov ebx, %[a0];"
		"int 0x80;"
		: [scnum]"+a"(num)
		: [a0]"m"(a0)
		: /* No clobbers */
	);
	return num;
	// int ret;
	// asm volatile(
	// 	"mov eax, %[scnum];"
	// 	"mov ebx, %[a0];"
	// 	"int 0x80;"
	// 	: "+a"(ret)
	// 	: [scnum]"a"(num),
	// 	[a0]"m"(a0)
	// 	: /* No clobbers */
	// );
	// return ret;
}

static inline int syscall2(int num, int a0, int a1) {
	asm volatile(
		"mov eax, %[scnum];"
		"mov ebx, %[a0];"
		"mov ebc, %[a1];"
		"int 0x80;"
		: [scnum]"+a"(num)
		: [a0]"m"(a0),
		  [a1]"m"(a1)
		: /* No clobbers */
	);
	return num;
}

static inline int syscall3(int num, int a0, int a1, int a2) {
	asm volatile(
		"mov eax, %[scnum];"
		"mov ebx, %[a0];"
		"mov ecx, %[a1];"
		"mov edx, %[a2];"
		"int 0x80;"
		: [scnum]"+a"(num)
		: [a0]"m"(a0),
		  [a1]"m"(a1),
		  [a2]"m"(a2)
		: /* No clobbers */
	);
	return num;
}

static inline int syscall4(int num, int a0, int a1, int a2, int a3) {
	asm volatile(
		"mov eax, %[scnum];"
		"mov ebx, %[a0];"
		"mov ecx, %[a1];"
		"mov edx, %[a2];"
		"mov esi, %[a3];"
		"int 0x80;"
		: [scnum]"+a"(num)
		: [a0]"m"(a0),
		  [a1]"m"(a1),
		  [a2]"m"(a2),
		  [a3]"m"(a3)
		: /* No clobbers */
	);
	return num;
}

static inline int syscall5(int num, int a0, int a1, int a2, int a3, int a4) {
	asm volatile(
		"mov eax, %[scnum];"
		"mov ebx, %[a0];"
		"mov ecx, %[a1];"
		"mov edx, %[a2];"
		"mov esi, %[a3];"
		"mov edi, %[a4];"
		"int 0x80;"
		: [scnum]"+a"(num)
		: [a0]"m"(a0),
		  [a1]"m"(a1),
		  [a2]"m"(a2),
		  [a3]"m"(a3),
		  [a4]"m"(a4)
		: /* No clobbers */
	);
	return num;
}

static inline int syscall6(int num, int a0, int a1, int a2, int a3, int a4, int a5) {
	asm volatile(
		"mov eax, %[scnum];"
		"mov ebx, %[a0];"
		"mov ecx, %[a1];"
		"mov edx, %[a2];"
		"mov esi, %[a3];"
		"mov edi, %[a4];"
		"push ebp;"
		"mov ebp, %[a5];"
		"int 0x80;"
		"pop ebp;"
		: [scnum]"+a"(num)
		: [a0]"m"(a0),
		  [a1]"m"(a1),
		  [a2]"m"(a2),
		  [a3]"m"(a3),
		  [a4]"m"(a4),
		  [a5]"m"(a5)
		: /* No clobbers */
	);
	return num;
}

#define SYS_EXIT   0
#define SYS_SLEEP  1
#define SYS_READ   2
#define SYS_WRITE  3

#endif
