//
//  mman.h
//  xcode
//
//  Created by Valtteri Koskivuori on 26/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include <stddef.h>
#include "stdint.h"

#define KB 1024
#define MB (1024 * 1024)

#define PAGE_SIZE 4096

#define PAGE_ROUND_UP(x) ((((uint32_t)(x)) + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1)))
#define PAGE_ROUND_DN(x) (((uint32_t)(x)) & (~(PAGE_SIZE - 1)))

#define VIRT_OFFSET 0xC0000000
#define PFA_VIRT_OFFSET 0xD0000000

#define P2V(addr) ((void *)(((char *)(addr)) + VIRT_OFFSET))
#define V2P(addr) (((uint32_t)(addr)) - VIRT_OFFSET)

#define VA_PD_IDX(va) ((virt_addr)((va) & 0xFFC00000) >> 22)
#define VA_PT_IDX(va) ((virt_addr)((va) & 0x003FF000) >> 12)
#define VA_PG_OFF(va) ((virt_addr)((va) & 0x00000FFF))

#define PTE_PRESENT           (0x1 << 0)
#define PTE_WRITABLE          (0x1 << 1)
#define PTE_USER              (0x1 << 2)
#define PTE_DISABLE_WT        (0x1 << 3)
#define PTE_DISABLE_CACHE     (0x1 << 4)
#define PTE_ACCESSED          (0x1 << 5)

typedef uint32_t phys_addr;

// FIXME: I'm not sure why these packed unions don't work as I expect.
// Maybe get rid of them if I can't get them working.
// Or just get rid of them anyway.

// typedef union {
// 	uint32_t addr;
// 	struct {
// 		uint16_t pd_idx : 10;
// 		uint16_t pt_idx : 10;
// 		uint16_t offset : 12;
// 	} __attribute((packed)) idx;
// } __attribute((packed)) virt_addr;
typedef uint32_t virt_addr;

// typedef struct {
// 	phys_addr pte_addr: 20;
// 	unsigned char avl_2: 4;
// 	unsigned char is_4mb: 1;
// 	unsigned char available: 1;
// 	unsigned char accessed: 1;
// 	unsigned char cache_disabled: 1;
// 	unsigned char wt_disabled: 1;
// 	unsigned char user: 1;
// 	unsigned char writable: 1;
// 	unsigned char present: 1;
// } pde_t;

typedef uint32_t pde_t;
struct page_directory {
	pde_t entries[1024];
};

// typedef struct {
// 	phys_addr page_addr: 20;
// 	unsigned char avl: 3;
// 	unsigned char global: 1;
// 	unsigned char pat: 1;
// 	unsigned char dirty: 1;
// 	unsigned char accessed: 1;
// 	unsigned char cache_disabled: 1;
// 	unsigned char wt_disabled: 1;
// 	unsigned char user: 1;
// 	unsigned char writable: 1;
// 	unsigned char present: 1;
// } pte_t;

typedef uint32_t pte_t;
struct page_table {
	pte_t entries[1024];
};

// TODO: Consider renaming to flush_tlb?
static inline void flush_cr3(void) {
	asm volatile (
		"mov %%cr3, %%eax\n\t"
		"mov %%eax, %%cr3\n\t"
		: : : "eax"
	);
}

void *kmalloc(size_t bytes);

void kfree(void *ptr);
phys_addr get_physical_address(virt_addr virt);
