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

void *memcpy(void *dst, const void *src, size_t bytes);
void *memset(void *dst, int c, size_t n);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *str);

#include <vkern.h>
const char *dirname(v_ma *a, const char *path);
const char *basename(v_ma *a, const char *path);

#define bochsdbg() asm volatile("xchg bx, bx;")
