//
//  panic.h
//  minima
//
//  Created by Valtteri Koskivuori on 28/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include <terminal.h>
#include <idt.h>

#define panic(...) \
	__panic(__FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)

void __panic(const char *file, const char *func, uint32_t line, const char *fmt, ...);
