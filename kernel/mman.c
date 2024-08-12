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
struct multiboot_info *g_multiboot;
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

uint32_t *page_directory_base = (uint32_t *)&boot_page_directory;
uint32_t *page_table_base = (uint32_t *)&boot_page_table1;

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
	kprintf("Page directory:\n");
	for (size_t i = 0; i < 1024; ++i) {
		kprintf("%i: %h\n", i, page_directory_base[i]);
	}
}

struct vaddr {
	uint16_t pd_idx : 10;
	uint16_t pt_idx : 10;
	uint16_t offset : 12;
} __attribute__((packed));

phys_addr get_physical_address(virt_addr virt) {
	struct vaddr v = *(struct vaddr *)&virt;
	uint16_t pd_index = virt >> 22;
	uint16_t pt_index = virt >> 12 & 0x3FF; // 0x3FF to get only the last 10 bits
	
	// In boot.s, we map the last pd entry to the base of the page directory.
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	if (!(*page_directory & PD_PRESENT)) {
		kprintf("Last page directory entry isn't present.\n");
		panic();
	}
	// panic();
	uint32_t *page_table = ((uint32_t *)0xFFC00000) + (0x400 * pd_index);
	
	return (page_table[pt_index] & ~0xFFF) + ((uint32_t)virt & 0xFFF);
}

char *region_type_str(uint32_t type) {
	switch (type) {
		case MB_MEMORY_AVAILABLE: return "MB_MEMORY_AVAILABLE";
		case MB_MEMORY_RESERVED: return "MB_MEMORY_RESERVED";
		case MB_MEMORY_ACPI_RECLAIMABLE: return "MB_MEMORY_ACPI_RECLAIMABLE";
		case MB_MEMORY_NVS: return "MB_MEMORY_NVS";
		case MB_MEMORY_BADRAM: return "MB_MEMORY_BADRAM";
	}
	return "MB_MEMORY_UNKNOWN";
}

void dump_mem_regions() {
	uint32_t accumulator = 0;
	struct multiboot_mmap_entry *entries = (void *)g_multiboot->mmap_address;
	size_t n_entries = g_multiboot->mmap_length / sizeof(struct multiboot_mmap_entry);
	kprintf("%i entries:\n", n_entries);
	for (size_t i = 0; i < n_entries; ++i) {
		struct multiboot_mmap_entry *e = &entries[i];
		kprintf("type: %s %h-%h\n", region_type_str(e->type), e->addr_lo, e->addr_lo + e->length_lo);
		accumulator += e->type == MB_MEMORY_AVAILABLE ? e->length_lo : 0;
	}
	kprintf("%iKB total available\n", accumulator / 1024);
}

void init_mman(struct multiboot_info *mb) {
	kprintf("Initializing memory manager\n");
	g_multiboot = mb;
	mb_reserved_start = (uint32_t)g_multiboot;
	mb_reserved_end = (uint32_t)(g_multiboot + 1);
	kprintf("multiboot reserved start: %h, end: %h, size: %h\n", (void *)mb_reserved_start, (void *)mb_reserved_end, mb_reserved_end - mb_reserved_start);
	// kprintf("multiboot reserved end  : %h\n", (void *)mb_reserved_end);
	next_free_frame = 1;
	kprintf("Page frame allocator ready, setting up paging.\n");
	kprintf("Page directory lives at %h\n", (void *)page_directory_base);
	// TODO
	
	// dump_page_directory();
	dump_mem_regions();
}

// Based on https://anastas.io/osdev/memory/2016/08/08/page-frame-allocator.html

uint32_t mb_mmap_read(uint32_t request, uint8_t mode) {
	// Reserve frame 0 for errors, skip
	if (request == 0) return 0;
	
	// Invalid mode specified
	if (mode != MMAP_GET_NUM && mode != MMAP_GET_ADDR) return 0;
	
	uintptr_t current_mmap_address = (uintptr_t)g_multiboot->mmap_address + 0xC0000000;
	uintptr_t mmap_end_address = current_mmap_address + g_multiboot->mmap_length;
	uint32_t current_chunk = 0;
	
	while (current_mmap_address < mmap_end_address) {
		struct multiboot_mmap_entry *current_entry = (struct multiboot_mmap_entry *)current_mmap_address;
		
		// Split entry into 4KB chunks
		uint32_t current_entry_end = current_entry->addr_lo + current_entry->length_lo;
		for (uint32_t i = current_entry->addr_lo; i + PAGE_SIZE < current_entry_end; i += PAGE_SIZE) {
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
	if (bytes > 4096) return NULL;
	uint32_t new_frame = allocate_frame();
	uint32_t new_frame_addr = mb_mmap_read(new_frame, MMAP_GET_ADDR);
	kprintf("Allocated new page frame at %h\n", new_frame_addr);
	return (void *)new_frame_addr;
}

void kfree(void *ptr) {
	(void)ptr;
	ASSERT_NOT_REACHED();
}
