//
//  idt.c
//  xcode
//
//  Created by Valtteri on 25.1.2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "idt.h"
#include "io.h"
#include "terminal.h"

struct IDT_entry{
	uint16_t offset_lowerbits;
	uint16_t selector;
	unsigned char zero;
	unsigned char type_attr;
	unsigned short int offset_higherbits;
};

struct IDT_entry IDT[256];

// Stop interrupts
void cli(void) {
	asm("cli");
}

// Restore interrupts
void sti(void) {
	asm("sti");
}

// From irq.S
void load_stage0_gdt(void);

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

void idt_init(void) {
	extern int load_idt();
	extern int gp_hook();
	extern int pf_hook();
	extern int irq0();
	extern int irq1();
	extern int irq2();
	extern int irq3();
	extern int irq4();
	extern int irq5();
	extern int irq6();
	extern int irq7();
	extern int irq8();
	extern int irq9();
	extern int irq10();
	extern int irq11();
	extern int irq12();
	extern int irq13();
	extern int irq14();
	extern int irq15();
	
	unsigned long gp_hook_address;
	unsigned long pf_hook_address;
	unsigned long irq0_address;
	unsigned long irq1_address;
	unsigned long irq2_address;
	unsigned long irq3_address;
	unsigned long irq4_address;
	unsigned long irq5_address;
	unsigned long irq6_address;
	unsigned long irq7_address;
	unsigned long irq8_address;
	unsigned long irq9_address;
	unsigned long irq10_address;
	unsigned long irq11_address;
	unsigned long irq12_address;
	unsigned long irq13_address;
	unsigned long irq14_address;
	unsigned long irq15_address;
	unsigned long idt_address;
	unsigned long idt_ptr[2];
	
	// Remap the PICs
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
	
	// General Protection Fault
	gp_hook_address = (unsigned long)gp_hook;
	IDT[0xD].offset_lowerbits = gp_hook_address & 0xFFFF;
	IDT[0xD].selector = 0x08;
	IDT[0xD].zero = 0;
	IDT[0xD].type_attr = 0x8E;
	IDT[0xD].offset_higherbits = (gp_hook_address & 0xFFFF0000) >> 16;

	// Page Fault
	pf_hook_address = (unsigned long)pf_hook;
	IDT[0xE].offset_lowerbits = pf_hook_address & 0xFFFF;
	IDT[0xE].selector = 0x08;
	IDT[0xE].zero = 0;
	IDT[0xE].type_attr = 0x8E;
	IDT[0xE].offset_higherbits = (pf_hook_address & 0xFFFF0000) >> 16;
	
	irq0_address = (unsigned long)irq0;
	IDT[32].offset_lowerbits = irq0_address & 0xffff;
	IDT[32].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[32].zero = 0;
	IDT[32].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[32].offset_higherbits = (irq0_address & 0xffff0000) >> 16;
	
	irq1_address = (unsigned long)irq1;
	IDT[33].offset_lowerbits = irq1_address & 0xffff;
	IDT[33].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[33].zero = 0;
	IDT[33].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[33].offset_higherbits = (irq1_address & 0xffff0000) >> 16;
	
	irq2_address = (unsigned long)irq2;
	IDT[34].offset_lowerbits = irq2_address & 0xffff;
	IDT[34].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[34].zero = 0;
	IDT[34].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[34].offset_higherbits = (irq2_address & 0xffff0000) >> 16;
	
	irq3_address = (unsigned long)irq3;
	IDT[35].offset_lowerbits = irq3_address & 0xffff;
	IDT[35].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[35].zero = 0;
	IDT[35].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[35].offset_higherbits = (irq3_address & 0xffff0000) >> 16;
	
	irq4_address = (unsigned long)irq4;
	IDT[36].offset_lowerbits = irq4_address & 0xffff;
	IDT[36].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[36].zero = 0;
	IDT[36].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[36].offset_higherbits = (irq4_address & 0xffff0000) >> 16;
	
	irq5_address = (unsigned long)irq5;
	IDT[37].offset_lowerbits = irq5_address & 0xffff;
	IDT[37].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[37].zero = 0;
	IDT[37].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[37].offset_higherbits = (irq5_address & 0xffff0000) >> 16;
	
	irq6_address = (unsigned long)irq6;
	IDT[38].offset_lowerbits = irq6_address & 0xffff;
	IDT[38].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[38].zero = 0;
	IDT[38].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[38].offset_higherbits = (irq6_address & 0xffff0000) >> 16;
	
	irq7_address = (unsigned long)irq7;
	IDT[39].offset_lowerbits = irq7_address & 0xffff;
	IDT[39].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[39].zero = 0;
	IDT[39].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[39].offset_higherbits = (irq7_address & 0xffff0000) >> 16;
	
	irq8_address = (unsigned long)irq8;
	IDT[40].offset_lowerbits = irq8_address & 0xffff;
	IDT[40].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[40].zero = 0;
	IDT[40].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[40].offset_higherbits = (irq8_address & 0xffff0000) >> 16;
	
	irq9_address = (unsigned long)irq9;
	IDT[41].offset_lowerbits = irq9_address & 0xffff;
	IDT[41].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[41].zero = 0;
	IDT[41].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[41].offset_higherbits = (irq9_address & 0xffff0000) >> 16;
	
	irq10_address = (unsigned long)irq10;
	IDT[42].offset_lowerbits = irq10_address & 0xffff;
	IDT[42].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[42].zero = 0;
	IDT[42].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[42].offset_higherbits = (irq10_address & 0xffff0000) >> 16;
	
	irq11_address = (unsigned long)irq11;
	IDT[43].offset_lowerbits = irq11_address & 0xffff;
	IDT[43].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[43].zero = 0;
	IDT[43].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[43].offset_higherbits = (irq11_address & 0xffff0000) >> 16;
	
	irq12_address = (unsigned long)irq12;
	IDT[44].offset_lowerbits = irq12_address & 0xffff;
	IDT[44].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[44].zero = 0;
	IDT[44].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[44].offset_higherbits = (irq12_address & 0xffff0000) >> 16;
	
	irq13_address = (unsigned long)irq13;
	IDT[45].offset_lowerbits = irq13_address & 0xffff;
	IDT[45].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[45].zero = 0;
	IDT[45].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[45].offset_higherbits = (irq13_address & 0xffff0000) >> 16;
	
	irq14_address = (unsigned long)irq14;
	IDT[46].offset_lowerbits = irq14_address & 0xffff;
	IDT[46].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[46].zero = 0;
	IDT[46].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[46].offset_higherbits = (irq14_address & 0xffff0000) >> 16;
	
	irq15_address = (unsigned long)irq15;
	IDT[47].offset_lowerbits = irq15_address & 0xffff;
	IDT[47].selector = 0x08; /* KERNEL_CODE_SEGMENT_OFFSET */
	IDT[47].zero = 0;
	IDT[47].type_attr = 0x8e; /* INTERRUPT_GATE */
	IDT[47].offset_higherbits = (irq15_address & 0xffff0000) >> 16;
	
	/* fill the IDT descriptor */
	idt_address = (unsigned long)IDT;
	idt_ptr[0] = (sizeof (struct IDT_entry) * 256) + ((idt_address & 0xffff) << 16);
	idt_ptr[1] = idt_address >> 16;
	
	load_idt(idt_ptr);
	load_stage0_gdt();
}
