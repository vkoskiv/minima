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

void dump_page_directory(void);

typedef union {
	uint32_t addr;
	struct {
		uint16_t pd_idx : 10;
		uint16_t pt_idx : 10;
		uint16_t offset : 12;
	} __attribute((packed)) idx;
} virt_addr;
typedef uint32_t phys_addr;

void init_mman(uint16_t mem_kb);
void dump_phys_regions(void);

static inline void flush_cr3(void) {
	asm volatile (
		"mov %%cr3, %%eax\n\t"
		"mov %%eax, %%cr3\n\t"
		: : : "eax"
	);
}

void *kmalloc(size_t bytes);

void kfree(void *ptr);
