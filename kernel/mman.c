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
#include "panic.h"
#include <vkern.h>

void _kernel_physical_start(void);
void _kernel_physical_end(void);
phys_addr kernel_physical_start = (phys_addr)_kernel_physical_start;
phys_addr kernel_physical_end = (phys_addr)_kernel_physical_end;

// From linker.ld
void _address_space_start(void);
uint32_t address_space_start = (uint32_t)&_address_space_start;

// Virtual
void _kernel_virtual_start(void);
void _kernel_virtual_end(void);
uint32_t kernel_virtual_start = (uint32_t)&_kernel_virtual_start;
uint32_t kernel_virtual_end = (uint32_t)&_kernel_virtual_end;
// uint32_t page_table_ident[1024] __attribute((aligned(4096)));

#define MMAP_GET_NUM 0
#define MMAP_GET_ADDR 1

#define PAGE_ROUND_UP(x) ((((uint32_t)(x)) + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1)))
#define PAGE_ROUND_DN(x) (((uint32_t)(x)) & (~(PAGE_SIZE - 1)))

/* Anatomy of a virtual address
31                                  0
 |  10 bits |  10 bits |   12 bits  |
 |  PD idx  |  PT idx  |   offset   |
 
 */

phys_addr get_physical_address(virt_addr virt) {
	uint16_t pd_index = virt.idx.pd_idx;
	uint16_t pt_index = virt.idx.pt_idx;
	
	// In stage0.c, we map the last pd entry to the base of the page directory.
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	if (!(*page_directory & PD_PRESENT)) {
		kprintf("Last page directory entry isn't present.\n");
		panic();
	}
	uint32_t *page_table = ((uint32_t *)0xFFC00000) + (0x400 * pd_index);
	if (!(*page_table & PD_PRESENT)) {
		kprintf("Address %h not present\n", virt.addr);
		panic();
	}
	
	return (page_table[pt_index] & ~0xFFF) + ((uint32_t)virt.addr & 0xFFF);
}

#define VMA_START 0xD0000000

static V_ILIST(vma_list);

struct vma {
	virt_addr start;
	size_t size;
	v_ilist linkage;
};

// int map_range

// void *kmalloc(size_t bytes) {
// 	uint32_t pages = bytes / PAGE_SIZE;
	
// 	// if (bytes > 4096) return NULL;
// 	// uint32_t new_frame = allocate_frame();
// 	// uint32_t new_frame_addr = mb_mmap_read(new_frame, MMAP_GET_ADDR);
// 	// kprintf("Allocated new page frame at %h\n", new_frame_addr);
// 	// return (void *)new_frame_addr;
// 	(void)bytes;
// 	return NULL;
// }

void kfree(void *ptr) {
	(void)ptr;
	ASSERT_NOT_REACHED();
}

static inline virt_addr read_cr2(void) {
	virt_addr ret;
	asm volatile ("movl %%cr2, %0" : "=r"(ret));
	return ret;
}

/*
	bit 0 (P): 0 = non-present page, 1 = protection violation
	bit 1 (W/R): 0 = read, 1 = write
	bit 2 (U/S): 0 = supervisor, 1 = user
	bit 3 (RSVD): reserved bit violation
	bit 4 (I/D): instruction fetch (if supported)
*/
union pf_error {
	uint32_t value;
	struct {
		char present: 1;   // 0 = not present, 1 = protection violation
		char write: 1;     // 0 = read, 1 = write
		char user: 1;      // 0 = supervisor, 1 = user
		char res_write: 1;
		char insn_fetch: 1;
		char prot_key: 1;
		char ss: 1;
		char pad: 8;
		char sgx: 1;
		char reserved: 8;
	};
};

struct pf_regs {
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	union pf_error error;
	uint32_t eip, cs, eflags;
};

void dump_regs(struct pf_regs *r) {
	kprintf("\tedi: %h, esi: %h, ebp: %h, esp: %h,\n\tebx: %h, edx: %h, ecx: %h, eax: %h\n\terror: %h\n\teip: %h, cs: %h, eflags: %h\n",
	    r->edi, r->esi, r->ebp, r->esp, r->ebx, r->edx, r->ecx, r->eax,
		r->error.value,
		r->eip, r->cs, r->eflags);
}

void handle_gp_fault(void) {
	kprintf("GP FAULT\n");
	panic();
}

void handle_page_fault(struct pf_regs *r) {
	virt_addr cr2 = read_cr2();
	kprintf("PAGE FAULT, %s %s %s @ %h\n",
		r->error.user ? "user" : "kernel",
		r->error.present ? "PV" : "NP",
		r->error.write ? "write" : "read",
	    cr2);
	dump_regs(r);
	panic();
}
