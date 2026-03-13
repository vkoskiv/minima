#ifndef _X86_H_
#define _X86_H_

#include <stdint.h>
#include <mm/types.h>
#include <keyboard.h>
#include <io.h>

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
void sti_push(void);
void sti_pop(void);

// idt.c
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);

extern volatile int halted;

static inline void halt(void) {
	halted = 1;
	cli();
	// Mask all interrupts except keyboard, so CTRL+ALT+DEL
	// can still trigger reboot().
	// IRQs 8-15 are signaled via IRQ2, so masking that here
	// is enough, no need to touch the other interrupt controller.
	for (uint8_t i = 0; i < 8; ++i)
		if (i != 1)
			pic_mask_irq(i);
	sti();
	for (;;)
		asm("hlt");
}

static inline void reboot(void) {
	cli();
	uint8_t good = 0x02;
	while (good & 0x02)
		good = io_in8(0x64);
	io_out8(0x64, 0xFE);
	halt();
}

static inline int cmpxchg(volatile int *mem, int old, int new) {
	/*
		https://www.felixcloutier.com/x86/cmpxchg
		cmpxchg compares mem with eax (old), then:
		- if they were equal, store new into mem
		- otherwise, store mem into eax (old)

		The lock prefix ensures that all of this happens atomically,
		so this should be safe to run with interrupts enabled.
	*/
	asm volatile(
		"lock cmpxchg %[mem], %[new];"
		: [mem]"+m"(*mem), "+a"(old)
		: [new]"r"(new)
		: "memory"
	);
	return old;
}

void gdt_init(void);

#endif
