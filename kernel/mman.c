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

// Defined in linker.ld
void *_kernel_start;
void *_kernel_end;

void *kmalloc(size_t bytes) {
	(void)bytes;
	ASSERT_NOT_REACHED();
	return NULL;
}

void kfree(void *ptr) {
	(void)ptr;
	ASSERT_NOT_REACHED();
}

void init_mman(void) {
	kprint("Initializing memory manager. ");
	kprint("Kernel start: ");
	kprintaddr(&_kernel_start);
	kprint(", end: ");
	kprintaddr(&_kernel_end);
	kprint("\n");
}
