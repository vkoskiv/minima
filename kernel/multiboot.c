//
//  multiboot.c
//  xcode
//
//  Created by Valtteri Koskivuori on 28/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "multiboot.h"
#include "terminal.h"
#include "panic.h"

void validate_multiboot(uint32_t multiboot_magic, void *multiboot_header) {
	if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		kprint("FATAL: Not booted by a compliant bootloader, hanging.\n");
		panic();
	}
	
	struct multiboot_info *mb_header = (struct multiboot_info *)multiboot_header;
	if ((mb_header->flags & (1 << 6)) == 0) {
		// Bootloader forgot to give us a memory map. Not cool!
		kprint("FATAL: Bootloader didn't give us a memory map.\n");
		panic();
	}
}
