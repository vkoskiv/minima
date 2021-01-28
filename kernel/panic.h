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

static inline void panic(void) {
	kprint("PANIC - Halt and catch fire.\n");
	cli();
	for (;;) {
		asm("hlt");
	}
}
