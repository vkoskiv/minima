#include "panic.h"
#include "terminal.h"
#include "idt.h"
#include "sched.h"

static void halt(void) {
	cli();
	for (;;)
		asm("hlt");
}

void __panic(const char *file, const char *func, uint32_t line, const char *fmt, ...) {
	if (g_terminal_initialized) {
		if (file[0] && func[0])
			kprintf("PANIC(%s[%i]): %s[%s:%i]: ", current->name, current->id, func, file, line);
		else
			kprintf("PANIC: ");
		va_list args;
		va_start(args, fmt);
		kprintf_internal(fmt, args);
		va_end(args);
		kput('\n');
	}
	halt();
}
