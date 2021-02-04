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

// Multiboot info
struct multiboot_info *verified_header;
uint32_t mb_reserved_start;
uint32_t mb_reserved_end;

void _kernel_physical_start(void);
void _kernel_physical_end(void);
uint32_t kernel_physical_start = (uint32_t)&_kernel_physical_start;
uint32_t kernel_physical_end = (uint32_t)&_kernel_physical_end;

// Virtual
void _kernel_virtual_start(void);
void _kernel_virtual_end(void);
uint32_t kernel_virtual_start = (uint32_t)&_kernel_virtual_start;
uint32_t kernel_virtual_end = (uint32_t)&_kernel_virtual_end;

uint32_t next_free_frame;

// Paging
//uint32_t page_directory_base[1024] __attribute__((aligned(4096)));
//uint32_t page_table_base[1024] __attribute__((aligned(4096)));

/*void boot_page_directory(void);
void boot_page_table1(void);

uint32_t page_directory_base = (uint32_t)&boot_page_directory;
uint32_t page_table_base = (uint32_t)&boot_page_table1;
*/

uint32_t *page_directory_base;
uint32_t *page_table_base;

#define MMAP_GET_NUM 0
#define MMAP_GET_ADDR 1
#define PAGE_SIZE 4096

uint32_t mb_mmap_read(uint32_t request, uint8_t mode);
uint32_t allocate_frame();

// Defined in paging.s
extern void loadPageDirectory(unsigned int *);
extern void enablePaging();

void init_mman(void *multiboot_header) {
	kprintf("Initializing memory manager\n");
	verified_header = (struct multiboot_info *)multiboot_header;
	mb_reserved_start = (uint32_t)verified_header;
	mb_reserved_end = (uint32_t)(verified_header + sizeof(struct multiboot_info));
	kprintf("multiboot reserved start: %h\n", (void *)mb_reserved_start);
	kprintf("\nmultiboot reserved end  : %h\n", (void *)mb_reserved_end);
	next_free_frame = 1;
	kprintf("Page frame allocator ready, setting up paging.\n");
	
	page_directory_base = (uint32_t *)mb_mmap_read(allocate_frame(), MMAP_GET_ADDR);
	page_table_base = (uint32_t *)mb_mmap_read(allocate_frame(), MMAP_GET_ADDR);
	
	kprintf("Page directory lives at %h on frame %i\n", (void *)page_directory_base, (uint64_t)mb_mmap_read((uint32_t)page_directory_base, MMAP_GET_NUM));
	
	kprintf("Page table lives at %h on frame %i\n", (void *)page_table_base, (uint64_t)mb_mmap_read((uint32_t)page_table_base, MMAP_GET_NUM));
	
	// Blank page directory entries
	for (uint32_t i = 0; i < 1024; ++i) {
		// Sets flags Not present, Read/Write and Supervisor access.
		// See this handy diagram for all the flags: https://wiki.osdev.org/File:Page_dir.png
		page_directory_base[i] = 0x00000002;
	}
	
	// Identity map first 4MB, including the kernel.
	for (uint32_t i = 0; i < 1024; ++i) {
		page_table_base[i] = (i * 0x1000) | 3;
	}
	
	page_directory_base[0] = ((unsigned int)page_table_base) | 3;
	
	kprintf("Attempting to enable paging...\n");
	loadPageDirectory(page_directory_base);
	enablePaging();
	kprintf("Paging is enabled!\n");
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
