//
//  gdt.s
//  xcode
//
//  Created by Valtteri Koskivuori on 02/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

// Custom GDT setup
// Basically just set a 1:1 mapping of ring 0 for code and data

.align 8
gdt_start:

gdt_null:
	.long 0x00000000
	.long 0x00000000

gdt_kernel_code:
	// Base = 0x00000000, Limit 0xFFFFF * 4k = 4GiB
	.word 0xFFFF
	.word 0x0000
	.byte 0x00
	// TODO: Check DC bit, maybe want 1?
	//      P DPL S E DC RW A
	//      | |   | | |  |  |
	//      | |   | | |  | /
	//      | |   | | |  |/
	//      | |   | | | //
	//      | |   | | ///
	//      | |   | ////
	//      | |   /////
	//      | |  /////
	//      | | /////
	//      |/ /////
	//      || |||||
	.byte 0b10011010
	.byte 0b11001111
	.byte 0x00

gdt_kernel_data:
	.word 0xFFFF
	.word 0x0000
	.byte 0x00
	.byte 0b10010010
	.byte 0b11001111
	.byte 0x00

gdt_user_code:
	.word 0xFFFF
	.word 0x0000
	.byte 0x00
	.byte 0b11111010
	.byte 0b11001111
	.byte 0x00

gdt_user_data:
	.word 0xFFFF
	.word 0x0000
	.byte 0x00
	.byte 0b11110010
	.byte 0b11001111
	.byte 0x00

// TODO: Task state segment goes here

gdt_end:

.global _asm_gdt_descriptor
_asm_gdt_descriptor:
	.word gdt_end - gdt_start - 1
	.long gdt_start + 0

.global asm_gdt_init
.type asm_gdt_init, @function
asm_gdt_init:
	cli
	lgdt _asm_gdt_descriptor
	ljmp $0x08, $.complete_flush
	sti
	ret

.complete_flush:
	movw $0x10, %ax;
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss
	ret
