//
//  interrupt_handler.c
//  xcode
//
//  Created by Valtteri Koskivuori on 24/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "terminal.h"

void interrupt_handler(void) {
	kprintf("Interrupt triggered!\n");
}
