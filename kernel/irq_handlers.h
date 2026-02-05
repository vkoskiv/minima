#include "stdint.h"
#include "idt.h"
#include "io.h"

static inline void eoi(uint8_t irq) {
	if (irq >= 8)
		io_out8(PIC2_CMD, PIC_EOI);
	io_out8(PIC1_CMD, PIC_EOI);
}

