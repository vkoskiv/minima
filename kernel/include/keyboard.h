//
//  keyboard.h
//
//  Created by Valtteri Koskivuori on 25/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#ifndef _KEYBOARD_H_
#define _KEYBOARD_H_

#include <stdint.h>
#include <fs/dev_char.h>

#define SCANCODE_ESC 0x1B
#define KBD_IRQ (IRQ0_OFFSET + 1)

void kbd_init(void);
void received_scancode(uint8_t scancode);

// Spawn a task that types input from debug.h DEBUG_KEYSTROKES macro
void keyboard_debug_keystrokes(void);

#endif
