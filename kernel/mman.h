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

#define PD_PRESENT 0x1
#define PD_READWRITE 0x2
#define PD_USR_SUP 0x4
#define PD_WRTHROUGH 0x8
#define PD_CACHE 0x10
#define PD_ACCESSED 0x20

typedef union {
	uint32_t addr;
	struct {
		uint16_t pd_idx : 10;
		uint16_t pt_idx : 10;
		uint16_t offset : 12;
	} __attribute((packed)) idx;
} virt_addr;
typedef uint32_t phys_addr;

static inline void flush_cr3(void) {
	asm volatile (
		"mov %%cr3, %%eax\n\t"
		"mov %%eax, %%cr3\n\t"
		: : : "eax"
	);
}

void *kmalloc(size_t bytes);

void kfree(void *ptr);
