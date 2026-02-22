#ifndef _X86_H_
#define _X86_H_

#include <mman.h>

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28

/*
	NOTE: Most of the fields in this struct are only needed by i386 hardware
	task switching, which we don't use. We just set esp0 to the kernel stack
	of a process before switching to it, which then determines which stack
	the CPU switches to when interrupted while running user mode code.
*/
struct tss {
	uint16_t prev_tss, padding0;
	uint32_t esp0; // <--- CPU loads this esp on interrupt in CPL 3
	uint16_t ss0, padding1;
	uint32_t esp1;
	uint16_t ss1, padding2;
	uint32_t esp2;
	uint16_t ss2, padding3;
	uint32_t cr3;
	uint32_t eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
	uint16_t es;  uint16_t padding4;
	uint16_t cs;  uint16_t padding5;
	uint16_t ss;  uint16_t padding6;
	uint16_t ds;  uint16_t padding7;
	uint16_t fs;  uint16_t padding8;
	uint16_t gs;  uint16_t padding9;
	uint16_t ldt; uint16_t padding10;
	uint16_t T; uint16_t io_map_base;
	// TODO: ^ ?
} __attribute__((packed));

extern struct tss g_tss;
// For lidt & lgdt
struct descriptor_ptr {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed));

static inline virt_addr read_cr2(void) {
	virt_addr ret;
	asm volatile(
		"mov %[ret], cr2"
		: [ret]"=r"(ret)
		: /* No inputs */
		: /* No clobbers */
	);
	return ret;
}

#define EFLAGS_IF        (0x1 << 9)

static inline uint32_t read_eflags(void) {
	uint32_t eflags;
	asm volatile(
		"pushf;"
		"pop %0"
		: "=r"(eflags)
		: /* No inputs */
		: /* No clobbers */
	);
	return eflags;
}

// Stop interrupts
static inline void cli(void) {
	asm("cli");
}

// Restore interrupts
static inline void sti(void) {
	asm("sti");
}

void cli_push(void);
void cli_pop(void);

static inline void halt(void) {
	cli();
	for (;;)
		asm("hlt");
}

void gdt_init(void);

#endif
