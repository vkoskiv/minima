#ifndef _SYSCALLS_H_
#define _SYSCALLS_H_

#include <stdint.h>
#include <idt.h>

struct syscall {
	void *handler;
	uint8_t args;
};

extern struct syscall syscalls[];
void do_syscall(const struct irq_regs *const regs);

#define SYSCALL0(num) \
do { \
asm volatile( \
"mov eax, %[scnum];" \
"mov ebx, %[arg0];" \
"int 0x80;" \
: /* No outputs */ \
: [scnum]"a"(num) \
); \
} while (0)

#define SYSCALL1(num, a0) \
do { \
int av0 = a0; \
asm volatile( \
"mov eax, %[scnum];" \
"mov ebx, %[arg0];" \
"int 0x80;" \
: /* No outputs */ \
: [scnum]"a"(num), \
  [arg0] "m"(av0) \
); \
} while (0)


#define SYSCALL2(num, a0, a1) \
do { \
int av0 = a0; \
int av1 = a1; \
asm volatile( \
"mov eax, %[scnum];" \
"mov ebx, %[arg0];" \
"mov ecx, %[arg1];" \
"int 0x80;" \
: /* No outputs */ \
: [scnum]"a"(num), \
  [arg0] "m"(av0), \
  [arg1] "m"(av1) \
); \
} while (0)

#define SYSCALL3(num, a0, a1, a2) \
do { \
int av0 = a0; \
int av1 = a1; \
int av2 = a2; \
asm volatile( \
"mov eax, %[scnum];" \
"mov ebx, %[arg0];" \
"mov ecx, %[arg1];" \
"mov edx, %[arg2];" \
"int 0x80;" \
: /* No outputs */ \
: [scnum]"a"(num), \
  [arg0] "m"(av0), \
  [arg1] "m"(av1), \
  [arg2] "m"(av2) \
); \
} while (0)

#define SYSCALL4(num, a0, a1, a2, a3) \
do { \
int av0 = a0; \
int av1 = a1; \
int av2 = a2; \
int av3 = a3; \
asm volatile( \
"mov eax, %[scnum];" \
"mov ebx, %[arg0];" \
"mov ecx, %[arg1];" \
"mov edx, %[arg2];" \
"mov esi, %[arg3];" \
"int 0x80;" \
: /* No outputs */ \
: [scnum]"a"(num), \
  [arg0] "m"(av0), \
  [arg1] "m"(av1), \
  [arg2] "m"(av2), \
  [arg3] "m"(av3) \
); \
} while (0)

#define SYSCALL5(num, a0, a1, a2, a3, a4) \
do { \
int av0 = a0; \
int av1 = a1; \
int av2 = a2; \
int av3 = a3; \
int av4 = a4; \
asm volatile( \
"mov eax, %[scnum];" \
"mov ebx, %[arg0];" \
"mov ecx, %[arg1];" \
"mov edx, %[arg2];" \
"mov esi, %[arg3];" \
"mov edi, %[arg4];" \
"int 0x80;" \
: /* No outputs */ \
: [scnum]"a"(num), \
  [arg0] "m"(av0), \
  [arg1] "m"(av1), \
  [arg2] "m"(av2), \
  [arg3] "m"(av3), \
  [arg4] "m"(av4) \
); \
} while (0)

#define SYSCALL6(num, a0, a1, a2, a3, a4, a5) \
do { \
int av0 = a0; \
int av1 = a1; \
int av2 = a2; \
int av3 = a3; \
int av4 = a4; \
int av5 = a5; \
asm volatile( \
"mov eax, %[scnum];" \
"mov ebx, %[arg0];" \
"mov ecx, %[arg1];" \
"mov edx, %[arg2];" \
"mov esi, %[arg3];" \
"mov edi, %[arg4];" \
"push ebp;" \
"mov ebp, %[arg5];" \
"int 0x80;" \
"pop ebp;" \
: /* No outputs */ \
: [scnum]"a"(num), \
  [arg0] "b"(av0), \
  [arg1] "c"(av1), \
  [arg2] "d"(av2), \
  [arg3] "m"(av3), \
  [arg4] "m"(av4), \
  [arg5] "m"(av5) \
: "esi", "edi" \
); \
} while (0)

#define SYS_EXIT   0
#define SYS_SLEEP  1
#define SYS_READ   2
#define SYS_WRITE  3
#define SYS_HELLO1 42
#define SYS_HELLO2 43
#define SYS_HELLO3 44
#define SYS_HELLO4 45
#define SYS_HELLO5 46
#define SYS_HELLO6 47

#endif
