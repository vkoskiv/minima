//
//  gdt.s
//  xcode
//
//  Created by Valtteri Koskivuori on 02/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

// Custom GDT setup

gdt_start:

gdt_null:
	.long 0x0
	.long 0x0

gdt_code:
	.word 0xFFFF
	.word 0x0
	.word 0x0
	.byte 0b10011010
	.byte 0b11001111
	.byte 0x0

gdt_data:
	.word 0xFFFF
	.word 0x0
	.byte 0x0
	.byte 0b10010010
	.byte 0b11001111
	.byte 0x0

gdt_end:

.global gdt_descriptor
gdt_descriptor:
	.word gdt_end - gdt_start - 1
	.long gdt_start + 0

.global load_gdt
.type load_gdt, @function
load_gdt:
	lgdt gdt_descriptor + 0xC0000000
	ret
