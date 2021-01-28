//
//  mman.c
//  xcode
//
//  Created by Valtteri Koskivuori on 26/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "mman.h"
#include "assert.h"
#include <stddef.h>
#include "terminal.h"
#include "multiboot.h"

struct multiboot_info *verified_header;
uint32_t mb_reserved_start;
uint32_t mb_reserved_end;
uint32_t next_free_frame;

#define MMAP_GET_NUM 0
#define MMAP_GET_ADDR 1
#define PAGE_SIZE 4096

uint32_t mb_mmap_read(uint32_t request, uint8_t mode);
uint32_t allocate_frame();

void init_mman(void *multiboot_header) {
	kprint("Initializing memory manager\n");
	verified_header = (struct multiboot_info *)multiboot_header;
	mb_reserved_start = (uint32_t)verified_header;
	mb_reserved_end = (uint32_t)(verified_header + sizeof(struct multiboot_info));
	kprint("multiboot reserved start: "); kprintaddr((void *)mb_reserved_start);
	kprint("\nmultiboot reserved end  : "); kprintaddr((void *)mb_reserved_end); kprint("\n");
	next_free_frame = 1;
	
	kprint("Allocating some frames...\n");
	for (size_t i = 0; i < 8; ++i) {
		uint32_t new_frame = allocate_frame();
		uint32_t new_frame_address = mb_mmap_read(new_frame, MMAP_GET_ADDR);
		kprint("Allocated new frame at ");
		kprintaddr((void *)new_frame_address);
		kprint("\r");
	}
}

// Based on https://anastas.io/osdev/memory/2016/08/08/page-frame-allocator.html

uint32_t mb_mmap_read(uint32_t request, uint8_t mode) {
	// Reserve frame 0 for errors, skip
	if (request == 0) return 0;
	
	// Invalid mode specified
	if (mode != MMAP_GET_NUM && mode != MMAP_GET_ADDR) return 0;
	
	uintptr_t current_mmap_address = (uintptr_t)verified_header->mmap_address;
	uintptr_t mmap_end_address = current_mmap_address + verified_header->mmap_length;
	uint32_t current_chunk = 0;
	
	while (current_mmap_address < mmap_end_address) {
		struct multiboot_mmap_entry *current_entry = (struct multiboot_mmap_entry *)current_mmap_address;
		
		// Split entry into 4KB chunks
		uint64_t current_entry_end = current_entry->address + current_entry->length;
		for (uint64_t i = current_entry->address; i + PAGE_SIZE < current_entry_end; i += PAGE_SIZE) {
			if (mode == MMAP_GET_NUM && request >= i && request <= i + PAGE_SIZE) {
				// Return the frame number for a given address.
				return current_chunk + 1;
			}
			
			if (current_entry->type == MB_MEMORY_RESERVED) {
				if (mode == MMAP_GET_ADDR && current_chunk == request) {
					// Address of a chunk in reserved space was requested
					// Increment request until it's no longer reserved.
					++request;
				}
				// Skip to next chunk until we encounter a non-reserved one
				++current_chunk;
				continue;
			}
			
			if (mode == MMAP_GET_ADDR && current_chunk == request) {
				// Found a good a frame starting address, return it
				return i;
			}
			++current_chunk;
		}
		current_mmap_address += current_entry->size + sizeof(uintptr_t);
	}
	return 0;
}

uint32_t allocate_frame() {
	uint32_t current_address = mb_mmap_read(next_free_frame, MMAP_GET_ADDR);
	
	//Verify it's not in a reserved area
	if (current_address >= mb_reserved_start && current_address <= mb_reserved_end) {
		++next_free_frame;
		return allocate_frame();
	}
	// Grab the frame number for the address we found
	uint32_t current_chunk = mb_mmap_read(current_address, MMAP_GET_NUM);
	next_free_frame = current_chunk + 1;
	return current_chunk;
}

/*

 Memory map:
 Kernel: 0x00100000 -> 0x0010682C
 VGA buf:0x000B8000 -> 0x000B8FA0 (base + 4Kb, I think. 4K is from 80 x 25 * 16 bits)
 
*/

// Defined in linker.ld
extern void *_kernel_start;
extern void *_kernel_end;

void *kmalloc(size_t bytes) {
	(void)bytes;
	ASSERT_NOT_REACHED();
	return NULL;
}

void kfree(void *ptr) {
	(void)ptr;
	ASSERT_NOT_REACHED();
}
