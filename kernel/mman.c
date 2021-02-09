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
#include "panic.h"

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

// Boot.s regions
void boot_page_directory(void);
void boot_page_table1(void);

uint32_t next_free_frame;

// Paging
//uint32_t page_directory_base[1024] __attribute__((aligned(4096)));
//uint32_t page_table_base[1024] __attribute__((aligned(4096)));

/*void boot_page_directory(void);
void boot_page_table1(void);

uint32_t page_directory_base = (uint32_t)&boot_page_directory;
uint32_t page_table_base = (uint32_t)&boot_page_table1;
*/

uint32_t *page_directory_base = (uint32_t *)&boot_page_directory;
//uint32_t *page_table_base = (uint32_t *)&boot_page_table1;

#define MMAP_GET_NUM 0
#define MMAP_GET_ADDR 1
#define PAGE_SIZE 4096

uint32_t mb_mmap_read(uint32_t request, uint8_t mode);
uint32_t allocate_frame();

/* Anatomy of a virtual address
31                                  0
 |  10 bits |  10 bits |   12 bits  |
 |  PD idx  |  PT idx  |   offset   |
 
 */

#define PD_PRESENT 0x1
#define PD_READWRITE 0x2
#define PD_USR_SUP 0x4
#define PD_WRTHROUGH 0x8
#define PD_CACHE 0x10
#define PD_ACCESSED 0x20

void dump_page_directory(void) {
	for (size_t i = 0; i < 1024; ++i) {
		kprintf("%i: %h\n", (uint64_t)i, page_directory_base[i]);
	}
}

void *get_physical_address(void *virtual_address) {
	uint32_t pd_index = (uint32_t)virtual_address >> 22;
	uint32_t pt_index = (uint32_t)virtual_address >> 12 & 0x3FF; // 0x3FF to get only the last 10 bits
	
	// In boot.s, we map the last pd entry to the base of the page directory.
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	if (!(*page_directory & PD_PRESENT)) {
		kprintf("Last page directory entry isn't present.\n");
		panic();
	}
	panic();
	uint32_t *page_table = ((uint32_t *)0xFFC00000) + (0x400 * pd_index);
	
	return (void *)((page_table[pt_index] & ~0xFFF) + ((uint32_t)virtual_address & 0xFFF));
}

void dump_mem_regions() {
	uintptr_t head = (uintptr_t)verified_header->mmap_address;
	uintptr_t end  = head + verified_header->mmap_length;
	//uint32_t accumulator = 0;
	while (head < end) {
		struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)head;
		kprintf("start: %h, len: %h\n", entry->address, (uint64_t)entry->length);
		//accumulator += (uint32_t)entry->length;
		head += entry->size + sizeof(uintptr_t);
	}
	uint32_t accumulator = 1234;
	kprintf("Total: %iKB\n", accumulator);
}

void init_mman(struct multiboot_info *header) {
	kprintf("Initializing memory manager\n");
	verified_header = header;
	mb_reserved_start = (uint32_t)verified_header;
	mb_reserved_end = (uint32_t)(verified_header + sizeof(struct multiboot_info));
	kprintf("multiboot reserved start: %h\n", (void *)mb_reserved_start);
	kprintf("multiboot reserved end  : %h\n", (void *)mb_reserved_end);
	next_free_frame = 1;
	kprintf("Page frame allocator ready, setting up paging.\n");
	
	kprintf("Page directory lives at %h\n", (void *)page_directory_base);
	
	//dump_page_directory();
	dump_mem_regions();
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

void *kmalloc(size_t bytes) {
	(void)bytes;
	ASSERT_NOT_REACHED();
	return NULL;
}

void kfree(void *ptr) {
	(void)ptr;
	ASSERT_NOT_REACHED();
}
