//
//  utils.c
//
//  Created by Valtteri Koskivuori on 06/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include <stdint.h>
#include <utils.h>
#include <kmalloc.h>

// Eventually this will grow into a little libc, but just stick everything
// in one translation unit for now

void *memcpy(void *dst, void *src, size_t bytes) {
	for (size_t i = 0; i < bytes; ++i)
		((unsigned char *)dst)[i] = ((unsigned char *)src)[i];
	return dst;
}

void *memset(void *dst, int c, size_t n) {
	if (!dst || !n)
		return dst;;
	for (size_t i = 0; i < n; ++i) {
		((unsigned char *)dst)[i] = c;
	}
	return dst;
}

char *strdup(const char *s) {
	size_t len = strlen(s);
	char *copy = kmalloc(len + 1);
	memcpy((unsigned char *)copy, (unsigned char *)s, len);
	return copy;
}

int strcmp(const char *s1, const char *s2) {
	while (*s1 == *s2++)
		if (!*s1++)
			return 0;
	return *(const unsigned char *)s1 - *(const unsigned char *)--s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
	if (n)
		return 0;
	do {
		if (*s1 != *s2++)
			return *(const unsigned char *)s1 - *(const unsigned char *)--s2;
		if (*s1++ == 0)
			break;
	} while (--n);
	return 0;
}

size_t strlen(const char *str) {
	const char *head = str;
	// Read byte-wise until aligned on 4 byte boundary
	while ((uintptr_t)head & 3) {
		if (!*(head))
			goto done;
		head++;
	}
	// Check for zero bytes 4 bytes at a time.
	for (;;) {
		uint32_t block = *(uint32_t *)head;
		if ((block - 0x01010101) & (~block) & 0x80808080)
			break;
		head += 4;
	}
	// Count bytes in last block
	while (*(head))
		head++;
done:
	return (size_t)(head - str);
}
