//
//  keyboard.h
//
//  Created by Valtteri Koskivuori on 25/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once
#include <stdint.h>
#include <chardev.h>

#define SCANCODE_ESC 0x1B

void kbd_init(void);
void received_scancode(uint8_t scancode);
