//
//  kbd.h - PC keyboard driver API
//

#ifndef _KBD_H_
#define _KBD_H_

#include <stdint.h>

#define SCANCODE_ESC 0x1B
#define KBD_IRQ (IRQ0_OFFSET + 1)

void received_scancode(uint8_t scancode);

// Spawn a task that types input from debug.h DEBUG_KEYSTROKES macro
void keyboard_debug_keystrokes(void);

#endif
