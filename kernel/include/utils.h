//
//  utils.h
//
//  Created by Valtteri Koskivuori on 06/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include <stddef.h>

// NOTE: beware of side effects, as these evaluate operands twice
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

void *memcpy(unsigned char *dst, unsigned char *src, size_t bytes);
void *memset(unsigned char *dst, unsigned char c, size_t bytes);
int strcmp(const char *s1, const char *s2);
size_t strlen(const char *str);

#define bochsdbg() asm volatile("xchg bx, bx;")
