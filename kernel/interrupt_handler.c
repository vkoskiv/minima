//
//  interrupt_handler.c
//  xcode
//
//  Created by Valtteri Koskivuori on 24/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "interrupt_handler.h"
#include "terminal.h"

void interrupt_handler(void) {
	kprint("Interrupt triggered!\n");
}
