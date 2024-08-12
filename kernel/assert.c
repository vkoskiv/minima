//
//  assert.c
//  SPARC
//
//  Created by Valtteri on 28.1.2020.
//  Copyright © 2020 Valtteri Koskivuori. All rights reserved.
//

#include "assert.h"
#include "terminal.h"
#include "panic.h"

void assertFailed(const char *file, const char *func, int line, const char *expr) {
	kprintf("ASSERTION FAILED: In %s in function %s on line %i, expression \"%s\"\nHalting CPU\n", file, func, line, expr);
	panic();
}
