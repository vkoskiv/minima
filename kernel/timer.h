#include "stdint.h"
#include "idt.h"

#define SCHED_HZ 250
#define IRQ0_HZ 1000

extern uint32_t system_uptime_ms;

typedef struct {
	uint32_t w, d, h, m, s, ms;
} uptime_t;

#define S_MS 1000
#define M_MS (S_MS * 60)
#define H_MS (M_MS * 60)
#define D_MS (H_MS * 24)
#define W_MS (D_MS * 7)

static inline uptime_t get_uptime(void) {
	uint32_t ms = system_uptime_ms;
	uptime_t ut = { 0 };

	ut.w = ms / W_MS;
	ms -= (ut.w * W_MS);
	ut.d = ms / D_MS;
	ms -= (ut.d * D_MS);
	ut.h = ms / H_MS;
	ms -= (ut.h * H_MS);
	ut.m = ms / M_MS;
	ms -= (ut.m * M_MS);
	ut.s = ms / S_MS;
	ms -= (ut.s * S_MS);
	ut.ms = ms;

	return ut;
}

// Called by irq0_handler in idt.c
void do_timer(struct irq_regs regs);

void sleep(uint32_t ms);
