/*
	Programmable Interval Timer (Intel 8253/8254)
	We just channel 0 to a known frequency, and that
	triggers irq0 on every rising edge.

	Ports:
		0x40 - Channel 0 data, 16bit (rw)
		0x41 - Channel 0 data, 16bit (rw)
		0x42 - Channel 0 data, 16bit (rw)
		0x43 - Mode/Command,    8bit(w)
*/

#include <idt.h>
#include <kprintf.h>
#include <io.h>

void pit_initialize(void) {
	/*
		Mode 2 frequency = 1193182 / reload_value Hz
		I want 1kHz, so
		(3579545 / 3) / 1193 ≈ 1000.152277, close enough?
	*/

	// Configure PIT to channel 0, lobyte/hibyte, rate generator mode
	// It seems to already do that by default, but this can't hurt.
	io_out8(0x43, 0b00110100);

	uint16_t reload = 1193;
	io_out8(0x40, reload & 0xFF);
	io_out8(0x40, (reload & 0xFF00) >> 8);
	kprintf("pit: firing irq0 @ (3579545 / 3) / 1193 = ~1000.152277Hz\n");
}
