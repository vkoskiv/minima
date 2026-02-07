BITS 32
CPU 386
section .text

extern handle_gp_fault
global gp_hook
gp_hook:
	pusha
	call handle_gp_fault
	popa
	iret

extern handle_page_fault
global pf_hook
pf_hook:
	pusha
	push esp
	call handle_page_fault
	add esp, 4
	popa
	add esp, 4
	iret

global load_idt
load_idt:
	mov edx, [esp + 4]
	lidt [edx]
	sti
	ret

newgdt:
	dw 0x0018
	dd 0xC0007cb2

global remap_gdt
remap_gdt:
	lgdt [newgdt]
	ret

extern irq0_handler
global irq0
irq0:
	pusha
	call irq0_handler
	popa
	iret

extern irq1_handler
global irq1
irq1:
	pusha
	call irq1_handler
	popa
	iret

extern irq2_handler
global irq2
irq2:
	pusha
	call irq2_handler
	popa
	iret

extern irq3_handler
global irq3
irq3:
	pusha
	call irq3_handler
	popa
	iret

extern irq4_handler
global irq4
irq4:
	pusha
	call irq4_handler
	popa
	iret

extern irq5_handler
global irq5
irq5:
	pusha
	call irq5_handler
	popa
	iret

extern irq6_handler
global irq6
irq6:
	pusha
	call irq6_handler
	popa
	iret

extern irq7_handler
global irq7
irq7:
	pusha
	call irq7_handler
	popa
	iret

extern irq8_handler
global irq8
irq8:
	pusha
	call irq8_handler
	popa
	iret

extern irq9_handler
global irq9
irq9:
	pusha
	call irq9_handler
	popa
	iret

extern irq10_handler
global irq10
irq10:
	pusha
	call irq10_handler
	popa
	iret

extern irq11_handler
global irq11
irq11:
	pusha
	call irq11_handler
	popa
	iret

extern irq12_handler
global irq12
irq12:
	pusha
	call irq12_handler
	popa
	iret

extern irq13_handler
global irq13
irq13:
	pusha
	call irq13_handler
	popa
	iret

extern irq14_handler
global irq14
irq14:
	pusha
	call irq14_handler
	popa
	iret

extern irq15_handler
global irq15
irq15:
	pusha
	call irq15_handler
	popa
	iret
