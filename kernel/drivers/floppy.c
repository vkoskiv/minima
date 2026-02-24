#include <driver.h>
#include <kprintf.h>
#include <errno.h>
#include <idt.h>
#include <x86.h>
#include <drivers/cmos.h>
#include <assert.h>
#include <io.h>
#include <console.h>

/*
	Intel 8272A floppy controller driver

	Goal is to get this working on my physical 486 box.
	It has a GoldStar Prime 2C floppy/ide/serial card.
	Linux 6.x detects it as an 8272A, and based on my debugging, I'm inclined
	to agree that it's a pretty barebones floppy controller.
	Documentation: https://www.threedee.com/jcm/terak/docs/Intel%208272A%20Floppy%20Controller.pdf
	More of a datasheet, it's not very comprehensive on some things, but I can just try
	stuff to see what works instead.

	Newer 82078 docs (~1995): https://wiki.qemu.org/images/f/f0/29047403.pdf
	Better descriptions of registers, but contains a bunch of stuff my floppy controller
	doesn't support.
*/

struct floppy {
	uint8_t tracks;
	uint8_t heads;
	uint8_t sectors_per_track;
};

static uint16_t lba_to_head(struct floppy *f, uint16_t lba) {
	return lba % (2 * f->sectors_per_track);
}

static uint16_t lba_to_cyl(struct floppy *f, uint16_t lba) {
	return lba / (2 * f->sectors_per_track);
}

static uint16_t lba_to_sector(struct floppy *f,uint16_t lba) {
	return (lba % f->sectors_per_track) + 1;
}

#define IRQ_FDC 6

struct fdc_cmd {
	uint8_t cmd;
	uint8_t n_arg;
	uint8_t n_ret;
	uint8_t *args;
	uint8_t *ret;
};

// TODO: Might need to derive this from system clockspeed (pg. 49)
uint32_t fifo_retries = 100000;

int increase_fiforetries(void *ctx) {
	(void)ctx;
	fifo_retries += 1000;
	kprintf("fifo_retries: %i\n", fifo_retries);
	return 0;
}
int decrease_fiforetries(void *ctx) {
	(void)ctx;
	fifo_retries -= 1000;
	kprintf("fifo_retries: %i\n", fifo_retries);
	return 0;
}

#define CMD_READ_TRACK      0x02
#define CMD_SPECIFY         0x03
#define CMD_CHECK_STATUS    0x04
#define CMD_WRITE_DATA      0x05
#define CMD_READ_DATA       0x06
#define CMD_RECALIBRATE     0x07
#define CMD_SENSE_INTERRUPT 0x08
#define CMD_WRITE_DELETED   0x09
#define CMD_READ_DELETED    0x0C
#define CMD_FORMAT_TRACK    0x0D
#define CMD_DUMPREGS        0x0E
#define CMD_SEEK            0x0F
#define CMD_VERSION         0x10
#define CMD_VERIFY          0x16
// Register offsets from drive->io_base
// 82078 datasheet chapter 2.1
#define IO_OFF_RSVD0 0x00                                  |  7   |  6  |  5   |  4   |   3    |  2    |   1   |   0   |
// PS/2 and newer #define IO_OFF_SRB   0x01 // r/w 'Status Register B'       |RSVD  |RSVD | RSVD | RSVD |  RSVD  |IDLEMSK|  PD   | IDLE  |
#define IO_OFF_DOR   0x02 // r/w 'Digital Output Reg.'     |RSVD  |RSVD |MOTEN1|MOTEN0|DMAGATE#|RESET# | RSVD  |DRVSEL |
#define IO_OFF_TDR   0x03 // r/w 'Tape Drive Reg.'         |      |     |      |      |        |       |       |       |
#define IO_OFF_MSR   0x04 // r   'Main Status Reg.'        | RQM  | DIO |NONDMA|CMDBSY|  RSVD  | RSVD  |DRV1BSY|DRV2BSY|
// PS/2 and newer #define IO_OFF_DSR   0x04 // w   'Datarate Select Reg.'    |SWRST |PWRDN|PDOSC |PRCMP2| PRCMP1 |PRCMP0 |DRSEL1 |DRSEL2 |
#define IO_OFF_FIFO  0x05 // r/w                           |
#define IO_OFF_RSVD1 0x06                                  |
#define IO_OFF_DIR   0x07 // r   'Digital Input Reg.'      |DSKCHG| --- | ---  | ---- | -----  | ----- | ----- | ----- |
// Newer feature #define IO_OFF_CCR   0x07 // w
// FIXME: ccr is related to datarate, not mentioned in older 8272 doc?

/*
	MSR cheat sheet:
	RQM    - 1 = ready to send or receive, dir depends on DIO
	DIO    - 1 = fdc -> cpu, 0 = cpu -> fdc
	NDM    - 1 while executing non-dma transfer, goes to 0 to indicate end of exec phase for non-dma tfer
	BSY    - 1 = read or write in progress? doc doesn't say which is which. also called CMDBUSY and FDC Busy
	FD3BSY - 1 = fd3 in seek mode
	FD2BSY - 1 = fd2 in seek mode
	FD1BSY - 1 = fd1 in seek mode
	FD0BSY - 1 = fd0 in seek mode
*/

// Main Status Register
#define MSR_DRV0BSY  0x01 // 1 == seeking or recalibrating
#define MSR_DRV1BSY  0x02 // 1 == seeking or recalibrating
#define MSR_DRV2BSY  0x04 // 1 == seeking or recalibrating
#define MSR_DRV3BSY  0x08 // 1 == seeking or recalibrating
#define MSR_CMD_BUSY 0x10 // 1 == cmd in progress
#define MSR_NON_DMA  0x20
#define MSR_DIO      0x40 // when RQM == 1, 1 == read, 0 == write
#define MSR_RQM      0x80 // 1 == host can transfer data


struct floppy_drive {
	enum cmos_fd_type type;
	const char *name;
	uint16_t io_base;
	uint8_t version;
};

uint16_t known_fdc_ports[] = {
	0x3F0, // Drives A & B, or fd0, fd1
	0x370, // If a system has >2 drives, drives fd2, fd3
	0x360, // Quite rare, but would be fun to try?
};

/*
	The floppy controller in my 486 box has JP9 jumper
	to move the I/O from 0x3F0 -> 0x370, so my probe code
	needs to scan each ID to find drives.
	Doc: https://just42.net/jwoithe/prime2c/
	Specifically: https://just42.net/jwoithe/prime2c/KTV4Lp1.gif
	and: https://just42.net/jwoithe/prime2c/KTV4Lp2.gif
*/
struct floppy_controller {
	volatile uint32_t interrupted;
	uint16_t io_base;
	struct floppy_drive drives[2];
};

static int probe(v_ma *a);
struct driver floppy = {
	.name = "floppy",
	.probe = probe,
	.deps = {
		"cmos",
		NULL,
	}
};

int read_sectors(struct floppy_drive *d);
int write_sectors(struct floppy_drive *d);
int is_busy(struct floppy_drive *d);
int seek(struct floppy_drive *d, uint16_t trk);
int recalibrate(struct floppy_drive *d);

static int wait_for_ready_read(struct floppy_drive *d) {
	for (uint32_t i = 0; i < fifo_retries; ++i) {
		uint8_t status = io_in8(d->io_base + IO_OFF_MSR);
		if ((status & 0b11000000) == 0b11000000)
			return status;
	}
	return -1;
}

static int wait_for_ready_write(struct floppy_drive *d) {
	for (uint32_t i = 0; i < fifo_retries; ++i) {
		uint8_t status = io_in8(d->io_base + IO_OFF_MSR);
		if ((status & 0b11000000) == 0b10000000)
			return status;
	}
	return -1;
}

static uint8_t read_msr(struct floppy_drive *d) {
	return io_in8(d->io_base + IO_OFF_MSR);
}

static void dump_msr_bits(const char *spot, uint8_t msr) {
	kprintf("%s status: ", spot);
	if (msr & MSR_RQM)      kprintf(" RQM");
	if (msr & MSR_DIO)      kprintf(" DIO");
	if (msr & MSR_NON_DMA)  kprintf(" NDMA");
	if (msr & MSR_CMD_BUSY) kprintf(" CMDBSY");
	if (msr & MSR_DRV3BSY)  kprintf(" D3BSY");
	if (msr & MSR_DRV2BSY)  kprintf(" D2BSY");
	if (msr & MSR_DRV1BSY)  kprintf(" D1BSY");
	if (msr & MSR_DRV0BSY)  kprintf(" D0BSY");
	kput('\n');
}

static int send_byte(struct floppy_drive *d, uint8_t byte) {
	int status = wait_for_ready_write(d);
	if (status < 0) {
		kprintf("send was never ready\n");
		return -1;
	}
	dump_msr_bits("send_byte", status);
	io_out8(d->io_base + IO_OFF_FIFO, byte);
	return 0;
}

static int read_byte(struct floppy_drive *d) {
	int status = wait_for_ready_read(d);
	if (status < 0) {
		kprintf("read was never ready\n");
		return -1;
	}
	return io_in8(d->io_base + IO_OFF_FIFO);
	return 0;
}

static void write_dor(struct floppy_drive *d, uint8_t byte) {
	io_out8(d->io_base + IO_OFF_DOR, byte);
}

static int read_dor(struct floppy_drive *d) {
	int status = wait_for_ready_read(d);
	if (status < 0) {
		kprintf("read_dor was never ready\n");
		return -1;
	}
	return io_in8(d->io_base + IO_OFF_DOR);
}

// r/w 'Digital Output Reg.'     |RSVD  |RSVD |MOTEN1|MOTEN0|DMAGATE#|RESET# | RSVD  |DRVSEL |
static void motor_set(struct floppy_drive *d, int on) {
	switch (d->io_base) {
	 	case 0x3F0:
	 		write_dor(d, (0x0C | (on ? 0x10 : 0x00))); // MOTEN0 | DMAGATE | RESET
	 		break;
	 	case 0x370:
	 		write_dor(d, (0x0D | (on ? 0x20 : 0x00))); // MOTEN1 | DMAGAGE | RESET | DRVSEL
	}
}
static void configure_drive(struct floppy_drive *d);

static volatile int floppy_interrupts = 0;

/*
	8272A fires interrupts when it reaches result phase of:
	- read data command
	- read track command
	- read id command
	- read deleted data command
	- write data command
	- format a cylinder command
	- write deleted data command
	- scan commands
	Or:
	- when ready line of fdd changes state
	- end of seek & recalibrate commands
	- during executing in non-dma mode
*/
static void fdc_irq(struct irq_regs regs) {
	(void)regs;
	++floppy_interrupts;
}

static const uint32_t irq_wait_timeout = (1024*128);

static int wait_irq(void) {
	uint32_t loops = 0;
	while (floppy_interrupts <= 0) {
		if (++loops >= irq_wait_timeout)
			return -1;
	}
	--floppy_interrupts;
	asm("":::"memory"); // Probably not needed, but can't hurt
	return 0;
}

// Chapter 9.2, pg. 50
static void initialize(struct floppy_drive *drive, enum cmos_fd_type type, uint16_t io_base) {
	drive->name = fd_names[type];
	drive->type = type;
	drive->io_base = io_base;

	kprintf("drive %h msr: %h\n", drive->io_base, read_msr(drive));
	return;
	/*

	int dor = read_dor(drive);
	// Reset drive
	write_dor(drive, dor & ~0x04);
	uint32_t delay = 10000000;
	while (--delay);
	write_dor(drive, dor);
	
	write_ccr(drive, 0);

	while (!interrupted);
	kprintf("got interrupt now\n");
	interrupted = 0;

	assert(!interrupted);

	int ret = send_byte(drive, CMD_DUMPREGS);
	if (ret < 0)
		goto fail;
	ret = read_byte(drive);
	*/
	// int ret = send_byte(drive, CMD_VERSION);
	// if (ret < 0) {
	// 	kprintf("no VERSION, try DUMPREGS %h\n", drive->io_base);
	// 	ret = send_byte(drive, CMD_DUMPREGS);
	// 	if (ret < 0) {
	// 		kprintf("no DUMPREGS, fail\n");
	// 		goto fail;
	// 	}

	// 	for (int i = 0; i < 10; ++i)
	// 		kprintf("dumpreg %i: %h\n", i, read_byte(drive));
	// } else {
	// 	drive->version = read_byte(drive);
	// 	kprintf("drive %s fdc is %s\n", drive->name, drive->version == 0x90 ? "Enhanced 82077*" : "8272A/765A");
	// }

	
	kprintf("sti_pop\n");
	return;
fail:
	drive->type = cmos_fd_none;
	return;
}

static int probe(v_ma *a) {
	struct floppy_controller fdc = { 0 };
	int ret = attach_irq(IRQ0_OFFSET + IRQ_FDC, fdc_irq, "floppy");
	if (ret) {
		kprintf("floppy: failed to attach irq %i\n", IRQ0_OFFSET + IRQ_FDC);
		goto fail;
	}
	enum cmos_fd_type type = cmos_fd_type(CMOS_FD_A);
	if (type)
		initialize(&fdc.drives[CMOS_FD_A], type, 0x3F0);
	type = cmos_fd_type(CMOS_FD_B);
	if (type)
		initialize(&fdc.drives[CMOS_FD_B], type, 0x370);
	floppy.driver_state = v_put(a, struct floppy_controller, fdc);
	kprintf("floppy: ");
	kprintf("a: %s ", fdc.drives[CMOS_FD_A].type ? fdc.drives[CMOS_FD_A].name : "n/a");
	kprintf("b: %s ", fdc.drives[CMOS_FD_B].type ? fdc.drives[CMOS_FD_B].name : "n/a");
	kput('\n');
	return 0;
fail:
	return ret;
}

/*
	Debug menu stuff below, for manually running different commands with
	lots of debug output. Press 'P' to enter the menu from console.
*/

struct floppy_drive a = {
	.io_base = 0x3f0,
	.name = "fda",
};
struct floppy_drive b = {
	.io_base = 0x370,
	.name = "fdb",
};

static int dump_msr(void *ctx) {
	struct floppy_drive *drv = ctx;
	kprintf("%h msr: ", drv->io_base);
	dump_msr_bits("", read_msr(drv));
	return 0;
}

static int dump_fd_types(void *ctx) {
	enum cmos_fd_type type = cmos_fd_type(CMOS_FD_A);
	if (type) {
		kprintf("cmos says A is %s (%h), storing to a.\n", fd_names[type], type);
		a.type = type;
	}
	type = cmos_fd_type(CMOS_FD_B);
	if (type) {
		kprintf("cmos says B is %s (%h), storing to b.\n", fd_names[type], type);
		b.type = type;
	}
	return 0;
}

// "Step Rate Time" 0x0 = 16ms, 0x1 = 15ms, etc. (8272A page 11)
// i.e. time intervals between adjacent step pulses. range 1-16ms
#define srt_ms(ms) (0x10 - ms)

// "Head Unload Time". If I recall, this is a remnant of old 8 inch drives
// that had mains voltage AC spindle motors that were on all the time, so
// they had to have electronically actuated head unload to preserve the life
// of the r/w head & media. I only own a single 8" disk, and no 8" drives.
// 99.9% of all floppy drives have a human actuated head unload, and motor
// control is used to prolong life instead.
// 8272A page 11, range 16-240ms, this configures the interval between end
// of execution phase of a read/write cmd to when the system "unloads" the
// r/w head.
#define hut_ms(ms) ((ms) >> 4)

// "Head Load Time", i.e. interval from when head load signal hoes high and
// when read/write op starts. range 2-254ms in 2ms increments, where
// 0x01 = 2ms, 0x02 = 4ms, 0x03 = 6ms, 0xFE = 254ms.
// Range stop at 0xFE because the LSB of this value is the "ND" or "no dma"
// bit, which is set to high to disable dma, and low to enable.
#define hlt_ms(ms) ((ms) >> 2) 

// Parameters in this table are stolen from Linux drivers/block/floppy.c.
struct drive_params {
	uint8_t step_rate_time;
	uint8_t head_unload_time;
	uint8_t head_load_time;
	
} floppy_params[] = {
	[cmos_fd_none]     = { 0 },
	[cmos_fd_525_360]  = { srt_ms(8), hut_ms(16), hlt_ms(16) },
	[cmos_fd_525_1200] = { srt_ms(6), hut_ms(16), hlt_ms(16) },
	[cmos_fd_35_720]   = { srt_ms(3), hut_ms(16), hlt_ms(16) },
	[cmos_fd_35_1440]  = { srt_ms(4), hut_ms(16), hlt_ms(16) },
	[cmos_fd_35_2880]  = { srt_ms(3), hut_ms(8),  hlt_ms(16) }, // hey, you never know :D
	//                                                    ^ 15 on linux, btw, though 15/2 = 7.5!
};

/*
	http://bos.asmhackers.net/docs/floppy/docs/floppy_tutorial.txt
*/
int reset_fdc(void *ctx) {
	struct floppy_drive *drv = ctx;
	kprintf("attempting to reset %h\n", drv->io_base);

	assert(read_eflags() & EFLAGS_IF);

	write_dor(drv, 0x00); // disable fdc
	write_dor(drv, 0x0C); // enable fdc
	if (wait_irq()) {
		kprintf("reset initial wait_irq() timed out after %i loops\n", irq_wait_timeout);
		return -1;
	}

	// newer intel manual on the 802078, page 50, says to do this sense + read st0&pcn 4 times.
	for (int i = 0; i < 4; ++i) {
		send_byte(drv, CMD_SENSE_INTERRUPT);
		uint8_t st0 = read_byte(drv); // status register 0: |ic  |SE|EC|NR|HD|US1|US0|
		/*
			ic is top 2 bits:
				0b00 = normal termination
				0b01 = abnormal termination
				0b10 = invalid command
				0b11 = abnormal termination, ready signal from drive canged during command
			Then:
			SE = seek end (1)
			EC = equipment check (no trk0 sig or other fault)
			NR = was not ready, and a read/write command issued
			HD = head address
			US1 = unit select 1
			US0 = unit select 0, these two indicate selected drive.
		*/
		uint8_t pcn = read_byte(drv); // present cylinder number
		kprintf("%i: st0: %h, pcn: %h\n", i, st0, pcn);
	}

	// now configure drive
	send_byte(drv, CMD_SPECIFY);
	struct drive_params p = floppy_params[drv->type];
	uint8_t srt  = p.step_rate_time;
	uint8_t hut  = p.head_unload_time;
	uint8_t hlt  = p.head_load_time;
	uint8_t ndma = 0x01;
	send_byte(drv, ((srt << 4) | hut));
	send_byte(drv, ((hlt << 1) | ndma));

	// then recalibrate
	motor_set(drv, 1);
	send_byte(drv, CMD_RECALIBRATE);
	if (drv->io_base == 0x3f0)
		send_byte(drv, 0x00); // <- drive (0 0 0 0 0 0 ds1 ds0)
	else
		send_byte(drv, 0x01);

	if (wait_irq()) {
		kprintf("reset recalibrate wait_irq() timed out after %i loops\n", irq_wait_timeout);
		motor_set(drv, 0);
		return -1;
	}

	// finally, check interrupt status again
	send_byte(drv, CMD_SENSE_INTERRUPT);
	uint8_t st0_postrecalibrate = read_byte(drv);
	uint8_t pcn_postrecalibrate = read_byte(drv);

	
	kprintf("After:  st0: %h, pcn: %h\n", st0_postrecalibrate, pcn_postrecalibrate);
	kprintf("floppy IRQs: %i\n", irq_counts[38]);

	kprintf("Reset worked(?) turning off motor\n");
	motor_set(drv, 0);
	assert(read_eflags() & EFLAGS_IF);
	return 0;
}

int dump_flop_irqs(void *ctx) {
	(void)ctx;
	kprintf("floppy IRQs: %i\n", irq_counts[38]);
	return 0;
}

int clear_terminal(void *ctx) {
	(void)ctx;
	terminal_clear();
	return 0;
}
struct cmd_list fd_debug = {
	.name = "fd_debug",
	.cmds = {
		{ {}, 0, -1, NULL,      TASK(dump_fd_types),  "detect cmos fd types",  't', 0 },
		{ {}, 0, -1, &a,        TASK(dump_msr),  "dump A msr",               '1', 0 },
		{ {}, 0, -1, &b,        TASK(dump_msr),  "dump B msr",               '2', 0 },
		{ {}, 0, -1, &a,        TASK(reset_fdc),  "reset A fdc",               'q', 0 },
		{ {}, 0, -1, &b,        TASK(reset_fdc),  "reset B fdc",               'w', 0 },
		{ {}, 0, -1, NULL,      TASK(dump_flop_irqs),  "dump flop irqs",               'i', 0 },
		{ {}, 0, -1, NULL,      TASK(clear_terminal),  "clear screen",               'x', 0 },
		{ {}, 0, -1, NULL,      TASK(increase_fiforetries),  "fifo_retries += 1000", 'h', 0 },
		{ {}, 0, -1, NULL,      TASK(decrease_fiforetries),  "fifo_retries -= 1000", 'n', 0 },
		{ 0 },
	}
};

register_driver(floppy);
