#include "panic.h"
#include "terminal.h"
#include "idt.h"

void panic(void) {
	if (g_terminal_initialized)
		kprintf("PANIC\n");
	cli();
	for (;;) {
		asm("hlt");
	}
}
