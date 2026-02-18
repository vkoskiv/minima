#include "timer.h"
#include "terminal.h"
#include "irq_handlers.h"
#include "sched.h"
#include "panic.h"
#include "assert.h"
#include "x86.h"

/*
	actual hz is
	(3579545 / 3) / 1193 ≈ 1000.152277Hz

	meaning a single irq0 tick interval is:
	(((3579545Hz) / 3) / 1193) / 1000 ≈ 1.000152277 ms

	So irq0_ms = 1
	and then irq0_fractions = 0.000152277 * 2**32 ≈ 654024.7349
	Round that up, so 654025
*/

// When fractions overflows, add extra ms
// I'll allow irq0_fractions to overflow naturally and just monitor for overflows by comparing
// with irq0_fractions_prev
// FIXME: Make these compile-time calculated from timer.h SCHED_HZ and IRQ0_HZ
static const uint32_t irq0_fractions_per_tick = 654025;

// static const uint32_t irq0_ms_per_tick = 1; // just ++ for now

static uint32_t irq0_fractions_prev = 0;
static uint32_t irq0_fractions = 0;
uint32_t system_uptime_ms = 0;

// qalc
// > (2**32)ms
//   (2^32) milliseconds = 49 d + 17 h + 2 min + 47.296 s

void irq0_handler(struct irq0_regs regs) {
	system_uptime_ms++;
	irq0_fractions += irq0_fractions_per_tick;
	if (irq0_fractions < irq0_fractions_prev)
		system_uptime_ms++;
	irq0_fractions_prev = irq0_fractions;
	eoi(0);
	if (system_uptime_ms % 4 == 0)
		sched();
}

void sleep(uint32_t ms) {
	current->sleep_till = system_uptime_ms + ms;
	assert((read_eflags() & EFLAGS_IF));
	cli();
	sched();
	assert(!(read_eflags() & EFLAGS_IF));
	sti();
	if (!current->sleep_till)
		panic("!current->sleep_till");
	current->sleep_till = 0;
}
