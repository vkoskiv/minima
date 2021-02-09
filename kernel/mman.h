//
//  mman.h
//  xcode
//
//  Created by Valtteri Koskivuori on 26/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include <stddef.h>

#define KB 1024
#define MB (1024 * 1024)

struct multiboot_info;

void init_mman(struct multiboot_info *header);

void *kmalloc(size_t bytes);

void kfree(void *ptr);
