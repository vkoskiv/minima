#include "timer.h"
#include "terminal.h"
#include "irq_handlers.h"

static uint32_t ticks = 0;

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
volatile uint32_t timer0;

static uint32_t irq0_fractions_prev = 0;
static uint32_t irq0_fractions = 0;
uint32_t system_uptime_ms = 0;

// qalc
// > (2**32)ms
//   (2^32) milliseconds = 49 d + 17 h + 2 min + 47.296 s

void irq0_handler(void) {
	system_uptime_ms++;
	irq0_fractions += irq0_fractions_per_tick;
	if (irq0_fractions < irq0_fractions_prev)
		system_uptime_ms++;
	irq0_fractions_prev = irq0_fractions;
	// Presumably the scheduler would be invoked here?
	if (timer0)
		timer0--;
	eoi(0);
}

void sleep(uint32_t ms) {
	timer0 = ms;
	while (timer0)
		asm("hlt");
}
