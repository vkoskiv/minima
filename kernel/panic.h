//
//  panic.h
//  minima
//
//  Created by Valtteri Koskivuori on 28/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include "terminal.h"
#include "idt.h"

// TODO: char *
static inline void panic(void) {
	if (g_terminal_initialized)
		kprintf("PANIC\n");
	cli();
	for (;;) {
		asm("hlt");
	}
}
