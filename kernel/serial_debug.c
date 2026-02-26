//
//  serial_debug.c
//
//  Created by Valtteri Koskivuori on 03/02/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include <serial_debug.h>
#include <io.h>
#include <panic.h>
#include <idt.h>
#include <kprintf.h>
#include <assert.h>

/* v Edit these v */
#define PORT COM2
#define BAUD 38400
/* ^ Edit these ^ */

static int s_color_enabled = 0;
static int s_serial_found = 0;
static int s_emu_serial_found = 0;
static uint16_t s_port = 0;

#define RREG_RX            0x0
#define WREG_TX            0x0
#define RWREG_IRQ_ENABLE   0x1
// |-|-|-|-|Modem Status|Receiver Line Status|Transm. holding reg. empty|rx available|
#define RWREG_DLAB_DIV_LSB 0x0
#define RWREG_DLAB_DIV_MSB 0x1
#define RREG_IRQ_ID        0x2
// |       |      | | |                       |         |                |
// |FIFO buf state|-|-|timeout_pending_or_rsvd|irq_state|irq_pending_if_0|
//    ^                                          ^
//   00 = no fifo                                00 = modem status
//   01 = fifo enabled, but unusable (??)        01 = transmitter holding reg empty
//   10 = fifo enabled                           10 = rx available
//                                               11 = receiver line status

#define WREG_FIFOCTL       0x2
// |       |        | |        |             |             |           |
// |irq_trig_level|-|-|dma_mode|fifo_clear_tx|fifo_clear_rx|fifo_enable|
//   ^
//   IRQ trig level, when to send data available irq:
//   - 00 = 1 byte
//   - 01 = 4 byte
//   - 10 = 8 byte
//   - 11 = 14 byte

#define RWREG_LINECTL      0x3
// |dlab_latch|break|par|par|par|stop|dat|dat|
//
// for data, 00 = 5, 01 = 6, 10 = 7, 11 = 8

#define RWREG_MODEMCTL     0x4
// |-|-|-|loopback|out2_pin|out1_pin|rts|dtr|

#define RREG_LINESTATUS    0x5
// |err|tx_empty|thre|bi|fe|pe|oe|dr|

#define RREG_MODEMSTATUS   0x6
// |dcd|ri|dsr|cts|ddcd|teri|ddsr|dcts|
#define RWREG_SCRATCH      0x7

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8
#define COM5 0x5F8
#define COM6 0x4F8
#define COM7 0x5E8
#define COM8 0x4E8

static short get_irq(uint16_t port) {
	switch (port) {
	case COM1: return IRQ0_OFFSET + 4;
	case COM2: return IRQ0_OFFSET + 3;
	case COM3: return IRQ0_OFFSET + 4;
	case COM4: return IRQ0_OFFSET + 3;
	default: return -1;
	}
}

static const char *get_name(uint16_t port) {
	switch (port) {
	case COM1: return "COM1";
	case COM2: return "COM2";
	case COM3: return "COM3";
	case COM4: return "COM4";
	case COM5: return "COM5";
	case COM6: return "COM6";
	case COM7: return "COM7";
	case COM8: return "COM8";
	}
	return "COM?";
}

static void do_serial(struct irq_regs regs) {
	panic("serial");
}

static void set_up_irq(void) {
	short irq = get_irq(s_port);
	if (irq < 0)
		return;
	int ret = attach_irq(irq, do_serial, get_name(s_port));
	if (ret) {
		kprintf("failed to attach %s IRQ %i\n", get_name(s_port), irq);
	}
}

static uint8_t getreg(uint8_t reg) {
	return io_in8(s_port + reg);
}

static void setreg(uint8_t reg, uint8_t value) {
	io_out8(s_port + reg, value);
}

static void set_dlab() {
	uint8_t lc = getreg(RWREG_LINECTL);
	lc |= 0x80;
	setreg(RWREG_LINECTL, lc);
}

static void clear_dlab() {
	uint8_t lc = getreg(RWREG_LINECTL);
	lc &= ~0x80;
	setreg(RWREG_LINECTL, lc);
}

/*
	NOTE: QEMU ignores this. Bochs doesn't.
*/
static void set_baudrate(uint32_t baud) {
	const uint32_t base_clock = 115200;
	assert(!(base_clock % baud));
	uint32_t divisor = base_clock / baud;
	set_dlab();
	setreg(RWREG_DLAB_DIV_LSB, divisor & 0xFF);
	setreg(RWREG_DLAB_DIV_MSB, (divisor >> 8) & 0xFF);
	clear_dlab();
}

static void init_e9(void) {
	// Check for port 0xE9 hack, which is supported in QEMU and Bochs
	uint8_t test = io_in8(0xE9);
	if (test == 0xE9)
		s_emu_serial_found = 1;
}

static void prepare_serial_device(uint16_t port) {
	s_port = port;
	init_e9();
	set_up_irq();

	// Disable interrupts
	setreg(RWREG_IRQ_ENABLE, 0x00);
	set_baudrate(BAUD);

	// 8N1                  xBPPPSDD
	setreg(RWREG_LINECTL, 0b00000011);
	// Enable FIFO, IRQ at 14 bytes, no DMA.
	//                     ||
	//                     ||   //- clear tx & rx
	//                     vv   ||v enable
	setreg(WREG_FIFOCTL, 0b11000111);

	/*
                              out2 (hooks up IRQ)
                                  |
                                  | rts dtr
                                   \ | /
                                   | |/dtr
	*/setreg(RWREG_MODEMCTL, 0b00001011);

	// FIXME: Loopback test just sends 0xAE out the serial
	// port on the 486 box.
	// Now test loopback
	// io_out8(port + 4, 0x1E); // Enable loopback
	// io_out8(port + 0, 0xAE); // Send 0xAE
	// if (io_in8(port + 0) == 0xAE) // Got 0xAE back?
	// 	s_serial_found = 1;
	// io_out8(port + 4, 0x0F); // Set to operating mode
	s_serial_found = 1;
}

void serial_setup(void) {
	prepare_serial_device(PORT);
}

void serial_out_byte(char c) {
	// Send to emulator output, if available
	if (s_emu_serial_found)
		io_out8(0xE9, c);
	if (!s_serial_found)
		return;
	// Wait for it to free up
	while((getreg(RREG_LINESTATUS) & 0x20) == 0);
	// Send it!
	setreg(WREG_TX, c);
}
