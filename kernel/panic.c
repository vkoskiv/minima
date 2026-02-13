#include "panic.h"
#include "terminal.h"
#include "idt.h"

static void halt(void) {
	cli();
	for (;;)
		asm("hlt");
}

void panic(const char *fmt, ...) {
	if (g_terminal_initialized) {
		kprintf("PANIC: ");
		va_list args;
		va_start(args, fmt);
		kprintf_internal(fmt, args);
		va_end(args);
		kput('\n');
	}
	halt();
}
