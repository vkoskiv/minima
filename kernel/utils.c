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

void *memcpy(void *dst, void *src, size_t bytes) {
	if (!bytes) return dst;
	for (size_t i = 0; i < bytes; ++i) {
		((uint8_t *)dst)[i] = ((uint8_t *)src)[i];
	}
	return dst;
}
