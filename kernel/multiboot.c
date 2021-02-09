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
#include "utils.h"

static struct multiboot_info copied_header;

struct multiboot_info *validate_multiboot(uint32_t multiboot_magic, void *multiboot_header) {
	if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		kprintf("FATAL: Not booted by a compliant bootloader, hanging.\n");
		panic();
	} else {
		kprintf("Valid multiboot header.\n");
	}
	
	struct multiboot_info *mb_header = (struct multiboot_info *)multiboot_header;
	if ((mb_header->flags & (1 << 6)) == 0) {
		// Bootloader forgot to give us a memory map. Not cool!
		kprintf("FATAL: Bootloader didn't give us a memory map.\n");
		panic();
	} else {
		kprintf("Valid memory map! Multiboot ptr is %h\n", multiboot_header);
	}
	kprintf("Original mmap_address: %h\n", mb_header->mmap_address);
	kprintf("Original mmap_length: %h\n", mb_header->mmap_length);
	// Copy this out, since we'll lose access when we drop identity mapping
	//memcpy(&copied_header, multiboot_header, sizeof(struct multiboot_info));
	//return &copied_header;
	return multiboot_header;
}
