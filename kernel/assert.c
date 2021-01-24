//
//  assert.c
//  SPARC
//
//  Created by Valtteri on 28.1.2020.
//  Copyright © 2020 Valtteri Koskivuori. All rights reserved.
//

#include "assert.h"
#include "terminal.h"

void assertFailed(const char *file, const char *func, int line, const char *expr) {
	kprint("ASSERTION FAILED: In ");
	kprint(file);
	kprint(" in function ");
	kprint(func);
	kprint(" on line ");
	kprintnum(line);
	kprint(", expression \"");
	kprint(expr);
	kprint("\"\n");
	kprint("Halting CPU");
	//TODO: wrapper func for CPU halt
	asm("hlt");
}
