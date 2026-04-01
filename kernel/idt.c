//
//  idt.c
//
//  Created by Valtteri on 25.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include <idt.h>
#include <io.h>
#include <kprintf.h>
#include <x86.h>
#include <keyboard.h>
#include <timer.h>
#include <sched.h>
#include <syscalls.h>
#include <errno.h>
#include <assert.h>
#include <mm/vma.h>
#include <kmalloc.h>

#define IDT_ENTRIES 256

struct idt_entry{
	uint16_t offset_lowerbits;
	uint16_t selector;
	unsigned char zero;
	unsigned char type_attr;
	unsigned short int offset_higherbits;
} __attribute__((packed));

struct idt_entry idt_entries[IDT_ENTRIES] __attribute__((aligned(16)));

#define DPL_KERNEL 0
#define DPL_USER   3

// 386 ref manual chapter 9.5 "IDT Descriptors"
#define IDT_PRESENT        0b10000000
#define IDT_TASK_GATE      0b00000101
#define IDT_INTERRUPT_GATE 0b00001110
#define IDT_TRAP_GATE      0b00001111
/* difference between interrupt gate & task gate is that for
   interrupt gates, IF gets cleared when entering interrupt, and restored
   from eflags on iret. For trap flags, IF isn't touched, meaning interrupts
   are enabled in the trap handler. (386 reference, chapter 9.6.1.3, pg. 160)*/
#define IDT_DPL(val)       ((val & 0x03) << 5)

#define IRQ_KERNEL (IDT_PRESENT | IDT_INTERRUPT_GATE | IDT_DPL(DPL_KERNEL))
#define IRQ_USER   (IDT_PRESENT | IDT_INTERRUPT_GATE | IDT_DPL(DPL_USER))

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
	.limit = IDT_ENTRIES * sizeof(idt_entries[0]) - 1,
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

#define PIC_OCW3_READ_IRR 0x0A
#define PIC_OCW3_READ_ISR 0x0B

#define CASCADE_IRQ 2

#define PIC1     0x20
#define PIC2     0xA0
#define PIC1_CMD PIC1
#define PIC2_CMD PIC2
#define PIC1_DAT (PIC1+1)
#define PIC2_DAT (PIC2+1)
#define PIC_EOI  0x20

static void pic_remap(void) {
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

static uint16_t pic_get_port(uint8_t *irq) {
	uint16_t port = *irq < 8 ? PIC1_DAT : PIC2_DAT;
	if (port == PIC2_DAT)
		*irq -= 8;
	return port;
}
/*
	Set Interrupt Mask Register (IMR) bit to 1 to tell the PIC
	to ignore that IRQ. The two PICs are cascaded, meaning all
	IRQs received by the slave PIC (IRQ 8-15) are signaled via
	the master PIC through its IRQ2, so masking IRQ2 will mask
	IRQs 8-15.
*/
void pic_mask_irq(uint8_t irq) {
	uint16_t port = pic_get_port(&irq);
	uint8_t mask = io_in8(port) | (0x1 << irq);
	io_out8(port, mask);
}

// FIXME: This doesn't seem to work? Don't think I ever tested it.
void pic_unmask_irq(uint8_t irq) {
	uint16_t port = pic_get_port(&irq);
	uint8_t mask = io_in8(port) & ~(1 << irq);
	io_out8(port, mask);
}

static uint16_t pic_get_irq_reg(int ocw3) {
	io_out8(PIC1_CMD, ocw3);
	io_out8(PIC2_CMD, ocw3);
	return (io_in8(PIC2) << 8) | io_in8(PIC1);
}

static uint16_t pic_get_irr(void) {
	return pic_get_irq_reg(PIC_OCW3_READ_IRR);
}

static uint16_t pic_get_isr(void) {
	return pic_get_irq_reg(PIC_OCW3_READ_ISR);
}

// Used in this file, and in sched.c task_init:
void pic_eoi(unsigned char irq) {
	if (irq >= IRQ0_OFFSET + 8)
		io_out8(PIC2_CMD, PIC_EOI);
	io_out8(PIC1_CMD, PIC_EOI);
}

asm(
".globl irq_common\n"
"irq_common:"
"	pushad;"
"	push ds;"
"	push es;"
"	push fs;"
"	push gs;"
"	push ebx;" // Move to kernel
"	mov bx, 0x10;" // >> 4 = idt_entries[1]
"	mov ds, bx;"
"	mov es, bx;"
"	mov fs, bx;"
"	mov gs, bx;"
"	pop ebx;"
"	push esp;"
"	call do_irq;"
"	add esp, 4;"
"	pop gs;"
"	pop fs;"
"	pop es;"
"	pop ds;"
"	popad;"
"	add esp, 8;" // irq_num & error
"	iret;"
);

void do_default(const struct irq_regs *const regs);
asm(
".globl do_default\n"
"do_default:"
"	ret;"
);

void do_panic(const struct irq_regs *const regs);

#define IRQ_LIST \
	  IRQ("div_err",           0, IRQ_KERNEL, do_panic  ) \
	  IRQ("dbg_exc",           1, IRQ_KERNEL, do_default) \
	  IRQ("nmi",               2, IRQ_KERNEL, do_panic  ) \
	  IRQ("breakpoint",        3, IRQ_KERNEL, do_default) \
	  IRQ("into_overflow",     4, IRQ_KERNEL, do_default) \
	  IRQ("bound_range",       5, IRQ_KERNEL, do_default) \
	  IRQ("invalid_opcode",    6, IRQ_KERNEL, do_panic  ) \
	  IRQ("copro_not_avail",   7, IRQ_KERNEL, do_default) \
	E_IRQ("double_fault",      8, IRQ_KERNEL, do_panic  ) \
	  IRQ("copro_seg_overrun", 9, IRQ_KERNEL, do_default) \
	E_IRQ("invalid_tss",      10, IRQ_KERNEL, do_default) \
	E_IRQ("seg_not_present",  11, IRQ_KERNEL, do_default) \
	E_IRQ("fault_stack",      12, IRQ_KERNEL, do_default) \
	E_IRQ("fault_gp",         13, IRQ_KERNEL, do_gp_fault) \
	E_IRQ("fault_page",       14, IRQ_KERNEL, do_page_fault) \
	  IRQ("copro_error",      16, IRQ_KERNEL, do_default) \
	  IRQ("irq0",             32, IRQ_KERNEL, do_default) \
	  IRQ("irq1",             33, IRQ_KERNEL, do_default) \
	  IRQ("irq2",             34, IRQ_KERNEL, do_default) \
	  IRQ("irq3",             35, IRQ_KERNEL, do_default) \
	  IRQ("irq4",             36, IRQ_KERNEL, do_default) \
	  IRQ("irq5",             37, IRQ_KERNEL, do_default) \
	  IRQ("irq6",             38, IRQ_KERNEL, do_default) \
	  IRQ("spurious7",        39, IRQ_KERNEL, do_default) \
	  IRQ("irq8",             40, IRQ_KERNEL, do_default) \
	  IRQ("irq9",             41, IRQ_KERNEL, do_default) \
	  IRQ("irq10",            42, IRQ_KERNEL, do_default) \
	  IRQ("irq11",            43, IRQ_KERNEL, do_default) \
	  IRQ("irq12",            44, IRQ_KERNEL, do_default) \
	  IRQ("irq13",            45, IRQ_KERNEL, do_default) \
	  IRQ("irq14",            46, IRQ_KERNEL, do_default) \
	  IRQ("spurious15",       47, IRQ_KERNEL, do_default) \
	  IRQ("syscall",         128, IRQ_USER,   do_syscall)

// CPU already pushed error, so just push irq_num
#define E_IRQ(name, num, attr, handler) \
void irq##num(void); \
asm( \
".globl irq" #num "\n" \
"irq" #num ":" \
"	push " #num ";" \
"	jmp irq_common;" \
);

// No error, push 0 to preserve alignment
#define IRQ(name, num, attr, handler) \
void irq##num(void); \
asm( \
".globl irq" #num "\n" \
"irq" #num ":" \
"	push 0;" \
"	push " #num ";" \
"	jmp irq_common;" \
);

IRQ_LIST

#undef E_IRQ
#undef IRQ

#define IRQ(name, num, attr, handler) \
	[num] = &(struct irq_handler){ name, attr, handler },

#define E_IRQ IRQ

struct irq_handler {
	const char *name;
	uint8_t attr;
	void (*handler)(const struct irq_regs *const);
};

struct irq_handler *irq_handlers[IDT_ENTRIES] = {
	IRQ_LIST
};

#undef E_IRQ
#undef IRQ

void do_panic(const struct irq_regs *const regs) {
	panic("%s at eip:%h", irq_handlers[regs->irq_num]->name, regs->eip);
}

void do_irq(const struct irq_regs *const regs) {
	irq_counts[regs->irq_num]++;
	if (regs->irq_num == IRQ0_OFFSET + 7 && !(pic_get_isr() & 0x80)) {
		return; // No EOI or handling, not a real IRQ.
	}
	if (regs->irq_num == IRQ0_OFFSET + 15 && !(pic_get_isr() & 0x8000)) {
		io_out8(PIC1_CMD, PIC_EOI); // Only signal EOI to master PIC
		return; // Not a real IRQ.
	}
	if (regs->irq_num >= IRQ0_OFFSET && regs->irq_num <= IRQ0_OFFSET + 15)
		pic_eoi(regs->irq_num);
	irq_handlers[regs->irq_num]->handler(regs);
}

#define NUM_IRQS (sizeof(irq_handlers) / sizeof(irq_handlers[0]))
uint32_t irq_counts[NUM_IRQS];

#define IRQ(name, num, attr, na) set_idt_entry(num, irq##num, attr);
#define E_IRQ IRQ

void idt_init(void) {
	pic_remap();
	memset((uint8_t *)&irq_counts[0], 0, NUM_IRQS * sizeof(irq_counts[0]));
	memset((uint8_t *)&idt_entries[0], 0, IDT_ENTRIES * sizeof(idt_entries[0]));

	IRQ_LIST

	load_idt(&idt_ptr);
}

int attach_irq(unsigned int irq, void (*handler)(const struct irq_regs *const), const char *name) {
	if (irq >= NUM_IRQS)
		return -EINVAL;
	assert(idt_entries[irq].type_attr);
	struct irq_handler *h = irq_handlers[irq];
	if (h && h->handler && h->handler != do_default)
		return -EEXIST;
	if (!h)
		h = kmalloc(sizeof(*h));
	h->handler = handler;
	h->attr = IRQ_KERNEL;
	h->name = name;
	irq_handlers[irq] = h;
	return 0;
}

#undef E_IRQ
#undef IRQ

// TODO: kprintf padding
int dump_irq_counts(void *ctx) {
	(void)ctx;
	kprintf("IRQ counts:\n");
	for (size_t i = 0; i < NUM_IRQS; ++i) {
		struct irq_handler *h = irq_handlers[i];
		if (!h)
			continue;
		kprintf("\t[%u]%s: %u\n", i, h->name ? h->name : "", irq_counts[i]);
	}
	return 0;
}
