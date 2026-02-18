#include "x86.h"
#include "panic.h"

static int s_cli = 0;
static int int_enabled = 0;

void cli_push(void) {
	int eflags = read_eflags();
	cli();
	// Store original state of eflags & check before calling
	// sti() in case interrupts were already disabled
	if (!s_cli)
		int_enabled = eflags & EFLAGS_IF;
	s_cli++;
}

void cli_pop(void) {
	if (read_eflags() & EFLAGS_IF)
		panic("cli_pop while interruptible");
	if (--s_cli < 0)
		panic("cli_pop undeflow");
	if (!s_cli && int_enabled)
		sti();
}

