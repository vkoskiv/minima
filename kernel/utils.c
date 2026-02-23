//
//  utils.c
//
//  Created by Valtteri Koskivuori on 06/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include <utils.h>
#include <stdint.h>

// Eventually this will grow into a little libc, but just stick everything
// in one translation unit for now

void *memcpy(unsigned char *dst, unsigned char *src, size_t bytes) {
	for (size_t i = 0; i < bytes; ++i)
		dst[i] = src[i];
	return dst;
}

void *memset(unsigned char *dst, unsigned char c, size_t bytes) {
	if (!dst || !bytes)
		return dst;;
	for (size_t i = 0; i < bytes; ++i) {
		dst[i] = c;
	}
	return dst;
}

int strcmp(const char *s1, const char *s2) {
	while (*s1 == *s2++)
		if (!*s1++)
			return 0;
	return *(const unsigned char *)s1 - *(const unsigned char *)--s2;
}

size_t strlen(const char *str) {
	size_t len = 0;
	while (str[len])
		len++;
	return len;
}
