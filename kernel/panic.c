#include <panic.h>
#include <kprintf.h>
#include <idt.h>
#include <sched.h>
#include <x86.h>

void __panic(const char *file, const char *func, uint32_t line, const char *fmt, ...) {
	cli();
	if (g_terminal_initialized) {
		if (current)
			kprintf("PANIC(%s[%i]): ", current->name, current->id);
		else
			kprintf("PANIC: ");
		if (file[0] && func[0])
			kprintf("%s[%s:%i]: ", func, file, line);
		va_list args;
		va_start(args, fmt);
		kprintf_internal(fmt, args);
		va_end(args);
		kput('\n');
	}
	halt();
}
