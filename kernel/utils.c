//
//  utils.c
//  xcode
//
//  Created by Valtteri Koskivuori on 06/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "utils.h"
#include "stdint.h"

// Eventually this will grow into a little libc, but just stick everything
// in one translation unit for now

void *memcpy(char *dst, char *src, size_t bytes) {
	while (bytes-- && (*dst++ = *src++));
	return dst;
}

void *memset(char *dst, char c, size_t bytes) {
	if (!dst || !bytes)
		return dst;;
	for (size_t i = 0; i < bytes; ++i) {
		dst[i] = c;
	}
	return dst;
}
