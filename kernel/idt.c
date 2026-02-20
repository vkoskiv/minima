//
//  idt.c
//
//  Created by Valtteri on 25.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "idt.h"
#include "io.h"
#include "terminal.h"
#include "x86.h"
#include "panic.h"
#include "utils.h"
#include "keyboard.h"
#include "timer.h"

#define IDT_ENTRIES 256

struct idt_entry{
	uint16_t offset_lowerbits;
	uint16_t selector;
	unsigned char zero;
	unsigned char type_attr;
	unsigned short int offset_higherbits;
} __attribute__((packed));

struct idt_entry idt_entries[IDT_ENTRIES] __attribute__((aligned(16)));

void set_idt_entry(uint8_t idx, void *handler, uint8_t attr) {
	idt_entries[idx] = (struct idt_entry){
		.offset_lowerbits = (uint32_t)handler & 0xFFFF,
		.selector = GDT_KERNEL_CODE,
		.zero = 0,
		.type_attr = attr,
		.offset_higherbits = (uint32_t)handler >> 16,
	};
}

static struct descriptor_ptr idt_ptr = {
	.limit = IDT_ENTRIES * sizeof(idt_entries[0]),
	.base = (uint32_t)&idt_entries[0],
};

void load_idt(struct descriptor_ptr *p);
asm(
".globl load_idt\n"
"load_idt:"
"	mov edx, [esp + 4];"
"	lidt [edx];"
"	ret;"
);

// From https://wiki.osdev.org/8259_PIC (obviously)

#define ICW1_ICW4      0x01
#define ICW1_SINGLE    0x02
#define ICW1_INTERVAL4 0x04
#define ICW1_LEVEL     0x08
#define ICW1_INIT      0x10

#define ICW4_8086       0x01
#define ICW4_AUTO       0x02
#define ICW4_BUF_SLAVE  0x08
#define ICW4_BUF_MASTER 0x0C
#define ICW4_SFNM       0x10

#define CASCADE_IRQ 2
#define IRQ0_OFFSET 0x20

#define PIC1     0x20
#define PIC2     0xA0
#define PIC1_CMD PIC1
#define PIC2_CMD PIC2
#define PIC1_DAT (PIC1+1)
#define PIC2_DAT (PIC2+1)
#define PIC_EOI  0x20

static void remap_pic(void) {
	const uint8_t offset_1 = 0x20;
	const uint8_t offset_2 = 0x28;

	io_out8(PIC1_CMD, ICW1_INIT | ICW1_ICW4); // Start init sequence
	io_wait();
	io_out8(PIC2_CMD, ICW1_INIT | ICW1_ICW4); // Same for slave PIC
	io_wait();
	io_out8(PIC1_DAT, offset_1); // ICW2, master PIC vector offset
	io_wait();
	io_out8(PIC2_DAT, offset_2); // ICW2, slave  PIC vector offset
	io_wait();
	io_out8(PIC1_DAT, 1 << CASCADE_IRQ);     // ICW3, let master know slave PIC is at IRQ2
	io_wait();
	io_out8(PIC2_DAT, 2); // ICW3, tell slave PIC its cascade identity (which I guess means it then knows it's at IRQ2?)
	io_wait();
	io_out8(PIC1_DAT, ICW4_8086); // ICW4, use 8086 mode (as opposed to 8080 mode). TODO: What's the difference?
	io_wait();
	io_out8(PIC2_DAT, ICW4_8086); // ICW4, same deal for slave PIC
	io_wait();

	// Set interrupt mask to 0x00 for both PICS, leaving all interrupts are unmasked.
	io_out8(PIC1_DAT, 0x0);
	io_out8(PIC2_DAT, 0x0);
}

void eoi(unsigned char irq) {
	if (irq >= 8)
		io_out8(PIC2_CMD, PIC_EOI);
	io_out8(PIC1_CMD, PIC_EOI);
}

void gp_hook(void);
asm(
".globl gp_hook\n"
"gp_hook:"
"	pusha;"
"	push esp;"
"	call handle_gp_fault;"
"	add esp, 4;"
"	popa;"
"	add esp, 4;"
"	iret;" // pop cs, eip, eflags. also (ss, esp) if privilege change occurs (not implemented yet)
);

void pf_hook(void);
asm(
".globl pf_hook\n"
"pf_hook:"
"	pusha;"
"	push esp;"
"	call handle_page_fault;"
"	add esp, 4;"
"	popa;"
"	add esp, 4;"
"	iret;"
);

/*struct irq_regs {
	// irq.S irq0 pushad/popad
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	// These below are popped by iret in irq.S
	void (*eip)(void);
	uint32_t cs, eflags; // usermode_esp, usermode_ss;?
}; */

void irq0_handler(void/*struct irq_regs regs*/) {
	timer_tick();
	eoi(0);
}

void irq0(void);
asm(
".globl irq0\n"
"irq0:"
"	pushad;"
"	call irq0_handler;"
"	popad;"
"	iret;"
);

#define IRQ_KEYBOARD 1
#define IRQ_CMOS_RTC 8
#define IRQ_MATHPROC 13

void do_irq(uint32_t irq_num) {
	switch (irq_num) {
	case IRQ_KEYBOARD: { // Keyboard
		uint8_t scancode = io_in8(0x60);
		received_scancode(scancode);
	} break;
	}
	eoi(irq_num);
}

#define DEF_IRQ(num) \
void irq##num(void); \
asm( \
".globl irq" #num "\n" \
"irq" #num ":" \
"	pushad;" \
"	push " #num ";" \
"	call do_irq;" \
"	add esp, 4;" \
"	popad;" \
"	iret;" \
)

DEF_IRQ(1);
DEF_IRQ(2);
DEF_IRQ(3);
DEF_IRQ(4);
DEF_IRQ(5);
DEF_IRQ(6);
DEF_IRQ(7);
DEF_IRQ(8);
DEF_IRQ(9);
DEF_IRQ(10);
DEF_IRQ(11);
DEF_IRQ(12);
DEF_IRQ(13);
DEF_IRQ(14);
DEF_IRQ(15);

void idt_init(void) {
	remap_pic();
	memset((uint8_t *)&idt_entries[0], 0, IDT_ENTRIES * sizeof(struct idt_entry));
	set_idt_entry(0xD, gp_hook, 0x8E); // general protection fault
	set_idt_entry(0xE, pf_hook, 0x8E); // page fault

	static void *irq_handlers[] = {
		irq0, irq1, irq2, irq3,
		irq4, irq5, irq6, irq7,
		irq8, irq9, irq10, irq11,
		irq12, irq13, irq14, irq15,
	};

	for (size_t i = 0; i < (sizeof(irq_handlers) / sizeof(irq_handlers[0])); ++i)
		set_idt_entry(IRQ0_OFFSET + i, irq_handlers[i], 0x8E);

	load_idt(&idt_ptr);
}
