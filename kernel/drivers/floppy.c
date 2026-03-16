#include <driver.h>
#include <kprintf.h>
#include <errno.h>
#include <idt.h>
#include <x86.h>
#include <drivers/cmos.h>
#include <assert.h>
#include <io.h>
#include <console.h>
#include <debug.h>
#include <timer.h>
#include <sched.h>
#include <dma.h>
#include <fs/dev_block.h>
#include <kmalloc.h>

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

	Notes:
	- Even though the 8272A can support up to 4 drives, in practice it supports only 2 drives.
	Originally, the shugart interface had the ability to configure up to 4 drives by setting jumpers
	on each drive. IBM wanted to cut cost and simplify this for the PC 5150, so they introduced
	the twist, and configured their drives to all respond to drive B signals. This limits the
	amount of drives per FDC to 2, which wasn't a problem for IBM in 1981, as the PC only had
	room for 2 drives, and the power supply was too weak to power more than that anyway.
	Bottom line is, the drive before the twist in the floppy cable appears as B, and the one after as A.

	8272A Registers, all 8 bits:
	
	Main Status Register (MSR), read-only, can be accessed any time
*/

static int s_verbose = 0;
static int s_cur_drive = 0;
static uint8_t selected_cyl = 0;

static void dbg(const char *fmt, ...) {
	if (!s_verbose)
		return;
	va_list args;
	va_start(args, fmt);
	kprintf_internal(fmt, args);
	va_end(args);
}

#define FDC_MAX_DRIVES 2
#define FDC_IRQ IRQ0_OFFSET + 6
#define FDC_DMA_CHANNEL 2
#define HANDLE_CYLINDER_RETRIES 20
#define SECTOR_CACHE_SECTORS 64

// Might want to bump up sector cache, 16 sectors is only
// 8192. Or select size based on RAM amount?

struct floppy_format {
	uint8_t cyls;
	uint8_t heads;
	uint8_t sectors_per_track;
	uint8_t sector_size;
	uint8_t gap_len;
	uint8_t datarate;
	const char *name;
};
// Some parameters in this table & drive_types below
// were stolen from Linux drivers/block/floppy.c.
static struct floppy_format floppy_formats[] = {
	[0] = { 0 },
	[1] = { 40, 2,  9, 2, 0x2A,    2, "PC360K 5.25\"" },
	[2] = { 40, 2,  9, 2, 0x23,    1, "AT360K 5.25\"" }, // also stretch 1?
	[3] = { 80, 2,  9, 2, 0x23,    1, "AT720K 5.25\"" },
	[4] = { 80, 2, 15, 2, 0x1B,    0, "1.2M 5.25\"" },
	[5] = { 80, 1,  9, 2, 0x2A,    2, "ss 360K 3.5\"" },
	[6] = { 80, 2,  9, 2, 0x2A,    2, "720K 3.5\"" },
	[7] = { 80, 2, 18, 2, 0x1B,    0, "1.44M 3.5\"" },
	[8] = { 80, 2, 36, 2, 0x1B, 0x43, "2.88M 3.5\"" },
};/*                               |
	                               datarate:
	                               0 = 500kb/s
	                               1 = 300kb/s
	                               2 = 250kb/s
	                               3 = 1Mb/s
*/
#define N_FLOPPY_FORMATS (sizeof(floppy_formats) / sizeof(floppy_formats[0]))

struct drive_params {
	uint16_t spinup_ms;
	int16_t motor_timeout_ms;
	uint8_t step_rate_time;
	uint8_t head_unload_time;
	uint8_t head_load_time;
	uint8_t detect_order[N_FLOPPY_FORMATS];
};

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

static struct drive_params drive_types[] = {
	[cmos_fd_none]     = { 0 },
	[cmos_fd_525_360]  = { 250, 1000, srt_ms(8), hut_ms(16), hlt_ms(16), {             2, 1, 0 }},
	[cmos_fd_525_1200] = { 250, 1000, srt_ms(6), hut_ms(16), hlt_ms(16), {       4, 3, 2, 1, 0 }},
	[cmos_fd_35_720]   = { 100, 1000, srt_ms(3), hut_ms(16), hlt_ms(16), {             6, 5, 0 }},
	[cmos_fd_35_1440]  = { 100, 1000, srt_ms(4), hut_ms(16), hlt_ms(16), {          7, 6, 5, 0 }},
	[cmos_fd_35_2880]  = { 100, 1000, srt_ms(3), hut_ms(8),  hlt_ms(16), {       8, 7, 6, 5, 0 }}, // hey, you never know :D
};

enum motor_state {
	fd_mot_off = 0,
	fd_mot_on,
	fd_mot_timeout,
};

struct sector_cache;

struct floppy_drive {
	enum cmos_fd_type type;
	const char *name;
	uint16_t io_base;
	int16_t motor_ms;
	enum motor_state motor_state;
	struct floppy_format *f;
	struct dev_block dev;
	struct sector_cache *cache;
	int8_t detect_idx;
	uint8_t num;
	uint8_t cyl;
	uint8_t st0;
	tid_t timer;
};

static struct floppy_drive drives[FDC_MAX_DRIVES] = { 0 };

uint16_t fdc_ports[] = {
	0x3F0, // Drives A & B, or fd0, fd1
	0x370, // If a system has >2 drives, drives fd2, fd3
	0x360, // Quite rare, but would be fun to try?
};
#define N_FDC_PORTS (sizeof(fdc_ports) / sizeof(fdc_ports[0]))

static uint16_t lba_to_head(struct floppy_format *f, uint16_t lba) {
	return (lba / f->sectors_per_track) % f->heads;
}

static uint16_t lba_to_cyl(struct floppy_format *f, uint16_t lba) {
	return lba / (f->heads * f->sectors_per_track);
}

static uint16_t lba_to_sector(struct floppy_format *f, uint16_t lba) {
	return (lba % f->sectors_per_track) + 1;
}

#define CMD_SCAN_EQUAL         0x01
#define CMD_READ_TRACK         0x02
#define CMD_SPECIFY            0x03
#define CMD_SENSE_DRIVE_STATUS 0x04
#define CMD_WRITE_DATA         0x05
#define CMD_READ_DATA          0x06
#define CMD_RECALIBRATE        0x07
#define CMD_SENSE_INTERRUPT    0x08
#define CMD_WRITE_DELETED      0x09
#define CMD_READ_ID            0x0A
#define CMD_READ_DELETED       0x0C
#define CMD_FORMAT_TRACK       0x0D
#define CMD_DUMPREGS           0x0E
#define CMD_SEEK               0x0F
#define CMD_VERSION            0x10
#define CMD_VERIFY             0x16
#define CMD_SCAN_LOW_OR_EQUAL  0x19
#define CMD_SCAN_HIGH_OR_EQUAL 0x1D

// 8272A manual page 12
// Interrupt Code, top 2 bits
#define ST0_IC(val) (val >> 6)
// Normal termination of command. Command was completed and properly executed.
#define ST0_IC_NORMAL (0x00)
// Set to 1 when CMD_SEEK is competed
#define ST0_IC_SEEK_END (0x01)
// "Invalid command issue. Command which was issued never started"
#define ST0_IC_INVALID (0x02)
// "Abnormal termination because during command execution the ready
// signal from FDD changed state"
#define ST0_IC_ABNORMAL (0x03)
#define ST0_SEEK_END (0x20)
#define ST0_EQUIPMENT_CHECK (0x10)
#define ST0_NOT_READY (0x08)
#define ST0_HEAD_ADDRESS (0x04)
#define ST0_DRIVE(val) (val & 0x03)

// FIXME: ST1-3 macros

// struct cmd_param {
// 	const char *name;
// 	uint8_t value;	
// };

// struct fdc_cmd_phase {
// 	int (*run)(struct fdc_cmd_phase *);
// 	int n_params;
// 	struct cmd_param params[16];
// };

// struct fdc_cmd {
// 	const char *name;
// 	int n_phases;
// 	struct fdc_cmd_phase phases[3];
// };

// static int cmd_send(struct fdc_cmd_phase *p) {
// 	(void)p;
// 	// TODO: loop params % send em
// 	return 0;
// }
// static int cmd_exec(struct fdc_cmd_phase *p) {
// 	(void)p;
// 	// TODO: sleep while waiting for IRQ
// 	return 0;
// }
// static int cmd_rslt(struct fdc_cmd_phase *p) {
// 	(void)p;
// 	return 0;
// }
// static struct fdc_cmd cmds[] = {
// 	[CMD_READ_DATA] = {
// 		.name = "read_data", .n_phases = 3,
// 		.phases = {
// 			{ .run = cmd_send, .n_params = 7, .params = { { .name = "C" }, { .name = "H" }, { .name = "R" } } },
// 			{ .run = cmd_exec, .n_params = 0 },
// 			{ .run = cmd_rslt, .n_params = 7, .params = { {0}, {0}} }
// 		}
// 	},
// };

// -----

// struct fdc_cmd {
// 	const char *name;
// 	uint8_t n_arg;
// 	uint8_t n_ret;
// 	struct cmd_param *args;
// 	struct cmd_param *rets;
// };

// static struct fdc_cmd cmds[] = {
// 	[CMD_READ_DATA] = {
// 		"read_data", .n_arg = 7, .n_ret = 7,
// 		.args = { },
// 		.rets = { }
// 	},
// 	[CMD_READ_DELETED] = {},
// 	[CMD_WRITE_DATA] = {},
// 	[CMD_WRITE_DELETED] = {},
// 	[CMD_READ_TRACK] = {},
// 	[CMD_READ_ID] = {},
// 	[CMD_FORMAT_TRACK] = {},
// 	[CMD_SCAN_EQUAL] = {},
// 	[CMD_SCAN_LOW_OR_EQUAL] = {},
// 	[CMD_SCAN_HIGH_OR_EQUAL] = {},
// 	[CMD_RECALIBRATE] = {},
// 	[CMD_SENSE_INTERRUPT] = {},
// 	[CMD_SPECIFY] = {},
// 	[CMD_SENSE_DRIVE_STATUS] = {},
// 	[CMD_SEEK] = {},
// };

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

void fdc_irq(struct irq_regs);
asm(
".extern floppy_interrupts\n"
".globl fdc_irq\n"
"fdc_irq:"
"	lock inc dword ptr [floppy_interrupts];"
"	ret;"
);

const uint32_t fifo_timeout_ms = 2000;
const uint32_t fifo_retry_delay = 10;
const uint32_t irq_timeout_ms = 2000;
const uint32_t irq_check_delay = 10;

/*
	FIXME: This buffer needs to:
	- live in the first 16MB of physical address space
	- not cross a 64k buffer

	I don't have the machinery to guarantee either of those, or
	allocate physically contiguous regions above PAGE_SIZE, so I'll
	allocate this statically for now.
*/

// For a multi-track read, we'll read a whole cylinder at a time, so at most,
// we'll read 2 tracks, 18 sectors per track, and 512 bytes per sector
// = 18432 bytes
// To ensure we're not crossing a 64k barrier, this buffer is aligned at
// 32k, which is the smallest power of 2 under 64k that fits our 18.4k cylinder buffer
#define DMA_BUF_SIZE (2 * 18 * 512)
uint8_t dma_buf[DMA_BUF_SIZE] __attribute__((aligned(0x8000)));
int8_t dma_buf_cyl = -1;
int8_t dma_buf_drv = -1;
phys_addr dma_buf_phys = 0;

static int wait_irq(void) {
	uint32_t slept_for_ms = 0;
	while (floppy_interrupts <= 0) {
		sleep(irq_check_delay);
		slept_for_ms += irq_check_delay;
		if (slept_for_ms >= irq_timeout_ms)
			return -1;
	}
	asm volatile(
		".extern floppy_interrupts\n"
		"lock dec dword ptr [floppy_interrupts];"
		: /* No outputs */
		: /* No inputs */
		: "memory"
	);
	return 0;
}

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
#define IO_OFF_CCR   0x07 // w
// My fdc has CCR even though it detects as an older chip

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
/*
	ST0 cheat sheet:
	
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

// Main Status Register
#define MSR_DRV0BSY  0x01 // 1 == seeking or recalibrating
#define MSR_DRV1BSY  0x02 // 1 == seeking or recalibrating
#define MSR_DRV2BSY  0x04 // 1 == seeking or recalibrating
#define MSR_DRV3BSY  0x08 // 1 == seeking or recalibrating
#define MSR_CMD_BUSY 0x10 // 1 == cmd in progress
#define MSR_NON_DMA  0x20
#define MSR_DIO      0x40 // when RQM == 1, 1 == read, 0 == write
#define MSR_RQM      0x80 // 1 == host can transfer data

/*
	The floppy controller in my 486 box has JP9 jumper
	to move the I/O from 0x3F0 -> 0x370, so my probe code
	needs to scan each ID to find drives.
	Doc: https://just42.net/jwoithe/prime2c/
	Specifically: https://just42.net/jwoithe/prime2c/KTV4Lp1.gif
	and: https://just42.net/jwoithe/prime2c/KTV4Lp2.gif
*/
// struct floppy_controller {
// 	volatile uint32_t interrupted;
// 	uint16_t io_base;
// 	struct floppy_drive drives[2];
// };

static int wait_for_ready_read(struct floppy_drive *d) {
	for (uint32_t i = 0; i < fifo_timeout_ms; i += fifo_retry_delay) {
		uint8_t status = io_in8(d->io_base + IO_OFF_MSR);
		if ((status & 0b11000000) == 0b11000000)
			return status;
		sleep(fifo_retry_delay);
	}
	return -1;
}

static int wait_for_ready_write(struct floppy_drive *d) {
	for (uint32_t i = 0; i < fifo_timeout_ms; i += fifo_retry_delay) {
		uint8_t status = io_in8(d->io_base + IO_OFF_MSR);
		if ((status & 0b11000000) == 0b10000000)
			return status;
		sleep(fifo_retry_delay);
	}
	return -1;
}

static uint8_t read_msr(struct floppy_drive *d) {
	int ret = io_in8(d->io_base + IO_OFF_MSR);
	io_wait();
	return ret;
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
		dbg("send was never ready\n");
		return -1;
	}
// #if DEBUG_FLOPPY == 1
// 	dump_msr_bits("send_byte", status);
// #endif
	io_out8(d->io_base + IO_OFF_FIFO, byte);
	io_wait();
	return 0;
}

static int read_byte(struct floppy_drive *d) {
	int status = wait_for_ready_read(d);
	if (status < 0) {
		dbg("read was never ready\n");
		return -1;
	}
	int ret = io_in8(d->io_base + IO_OFF_FIFO);
	io_wait();
	return ret;
}

// r/w 'Digital Output Reg.'     |RSVD  |RSVD |MOTEN1|MOTEN0|DMAGATE#|RESET# | RSVD  |DRVSEL |
static void write_dor(struct floppy_drive *d, uint8_t byte) {
	io_out8(d->io_base + IO_OFF_DOR, byte);
	io_wait();
}

static int read_dor(struct floppy_drive *d) {
	int status = wait_for_ready_read(d);
	if (status < 0) {
		dbg("read_dor was never ready\n");
		return -1;
	}
	int ret = io_in8(d->io_base + IO_OFF_DOR);
	io_wait();
	return ret;
}

static void motor_kill(struct floppy_drive *d) {
	switch (d->num) {
	case 0:
		write_dor(d, 0x0C); // MOTEN0 | DMAGATE | RESET
		break;
	case 1:
		write_dor(d, 0x0D); // MOTEN1 | DMAGAGE | RESET | DRVSEL
		break;
	default:
		assert(NORETURN);
	}
	d->motor_state = fd_mot_off;
}

static const uint32_t fdc_timeout_granularity = 250;
int floppy_timeout(void *ctx) {
	struct floppy_drive *d = ctx;
	while (1) {
		sleep(fdc_timeout_granularity);
		if (d->motor_state == fd_mot_timeout) {
			d->motor_ms -= fdc_timeout_granularity;
			dbg("drv %i ms: %i\n", d->num, d->motor_ms);
			if (d->motor_ms <= 0) {
				dbg("timeout(%i)\n", d->num);
				motor_kill(d);
				d->timer = 0;
				break;
			}
		}
	}
	return 0;
}

#if DEBUG_FLOPPY == 1
#define motor_set(drv, on) do { \
		kprintf("%s:%u: motor_set(%i, %i)\n", __FUNCTION__, __LINE__, (drv->num), (on)); \
		_motor_set(__FUNCTION__, (drv), (on)); \
	} while (0)
#else
#define motor_set(drv, on) _motor_set(__FUNCTION__, (drv), (on));
#endif

static const char *task_names[] = { "fd0_timeout", "fd1_timeout", "fd2_timeout", "fd3_timeout" };
static void _motor_set(const char *caller, struct floppy_drive *d, int on) {
	if (on) {
		if (d->motor_state == fd_mot_off) {
			switch(d->num) {
			case 0:
				write_dor(d, (0x0C | (on ? 0x10 : 0x00))); // MOTEN0 | DMAGATE | RESET
				break;
			case 1:
				write_dor(d, (0x0D | (on ? 0x20 : 0x00))); // MOTEN1 | DMAGAGE | RESET | DRVSEL
				break;
			default:
				assert(NORETURN);
			}
			sleep(drive_types[d->type].spinup_ms);
		}
		d->motor_state = fd_mot_on;
		return;
	}
	if (d->motor_state == fd_mot_timeout)
		kprintf("%s tried turning drive %i motor off while it was timing out\n", caller, d->num);
	d->motor_ms = drive_types[d->type].motor_timeout_ms;
	d->motor_state = fd_mot_timeout;
	if (!d->timer)
		d->timer = task_create(floppy_timeout, d, task_names[d->num], 0);
}

void write_ccr(struct floppy_drive *d, uint8_t byte) {
	io_out8(d->io_base + IO_OFF_CCR, byte);
}

#define ST3_FAULT (1 << 7)
#define ST3_WP    (1 << 6)
#define ST3_RDY   (1 << 5)
#define ST3_TRK0  (1 << 4)
#define ST3_TS    (1 << 3)
#define ST3_HS    (1 << 2)
#define ST3_DS   (0x3)
/*
	Reads ST3:
	|Fault|WP|RDY|T0|TS|HD|US1|US0|

	RDY = Ready (always 1 according to newer doc pg 47)
	TS = Two Side (always 1 according to newer doc pg 47)
	HD = side select, head address
*/
static void dump_st3(uint8_t st3) {
	if (st3 & ST3_FAULT)
		dbg("FAULT ");
	if (st3 & ST3_WP)
		dbg("WP ");
	if (st3 & ST3_RDY)
		dbg("RDY ");
	if (st3 & ST3_TRK0)
		dbg("TRK0 ");
	if (st3 & ST3_TS)
		dbg("TS ");
	if (st3 & ST3_HS)
		dbg("HeadAddr ");
	dbg("%1h ", st3 & ST3_DS);
	dbg("\n");
}

static void cmd_sense_status(const char *caller, struct floppy_drive *d) {
	int ret = send_byte(d, CMD_SENSE_DRIVE_STATUS);
	if (ret) {
		dbg("%s sense_status: CMD_SENSE_DRIVE_STATUS\n", caller);
		return;
	}
	ret = send_byte(d, d->num);

	if (ret) {
		dbg("%s sense_status: hds_ds1_ds0\n", caller);
		return;
	}

	ret = read_byte(d);
	if (ret < 0) {
		dbg("%s sense_status: read_byte\n", caller);
		return;
	}
	uint8_t st3 = ret;
	dbg("sense_status: ");
	dump_st3(st3);
	if (st3 & ST3_TRK0)
		d->cyl = 0;
	// TODO: ST3_WP?
}

static int flop_block_count(struct device *dev) {
	struct floppy_drive *d = dev->ctx;
	return d->f->cyls * d->f->heads * d->f->sectors_per_track;
}

static int flop_block_size(struct device *dev) {
	(void)dev;
	return 512;
}

int read_cyl(struct floppy_drive *d, uint8_t cyl);

#define SECTOR_DIRTY (0x1 << 0)

struct sector {
	v_ilist linkage;
	uint8_t *data;
	uint16_t lba;
	uint16_t flags;
};

struct sector_cache {
	v_ilist lru_sectors;
	uint16_t sector_size;
};

static struct sector_cache *sector_cache_init(uint32_t sector_size, uint32_t n_sectors) {
	struct sector_cache *c = kmalloc(sizeof(*c));
	c->lru_sectors = V_ILIST_INIT(c->lru_sectors);
	c->sector_size = sector_size;
	for (size_t i = 0; i < n_sectors; ++i) {
		struct sector *s = kmalloc(sizeof(*s));
		s->linkage = V_ILIST_INIT(s->linkage);
		s->lba = -1;
		s->flags = 0;
		s->data = kzalloc(sector_size);
		v_ilist_prepend(&s->linkage, &c->lru_sectors);
	}
	return c;
}

static void sector_cache_destroy(struct sector_cache *c) {
	v_ilist *pos, *temp;
	v_ilist_for_each_safe(pos, temp, &c->lru_sectors) {
		struct sector *s = v_ilist_get(pos, struct sector, linkage);
		v_ilist_remove(&s->linkage);
		kfree(s->data);
		kfree(s);
	}
	kfree(c);
}

static int flop_block_read(struct device *dev, unsigned int lba, unsigned char *out) {
	if (!out)
		return -EINVAL;
	struct floppy_drive *d = dev->ctx;
	uint16_t c = lba_to_cyl(d->f, lba);
	uint16_t h = lba_to_head(d->f, lba);
	uint16_t s = lba_to_sector(d->f, lba) - 1;

	if (c != dma_buf_cyl || d->num != dma_buf_drv) {
		int ret = read_cyl(d, c);
		if (ret)
			return ret;
	}

	uint16_t secbytes = flop_block_size(dev);
	uint16_t track_bytes = d->f->sectors_per_track * secbytes;
	uint8_t *src = &dma_buf[(h * track_bytes) + (s * secbytes)];
	memcpy((uint8_t *)out, src, secbytes);
	return 0;
}

static int sector_cache_read(struct device *dev, unsigned int lba, unsigned char *out) {
	if (!out)
		return -EINVAL;
	struct floppy_drive *d = dev->ctx;
	if (!d->cache)
		d->cache = sector_cache_init(flop_block_size(dev), SECTOR_CACHE_SECTORS);
	if (d->cache->sector_size != flop_block_size(dev)) {
		sector_cache_destroy(d->cache);
		d->cache = sector_cache_init(flop_block_size(dev), SECTOR_CACHE_SECTORS);
	}
	struct sector_cache *c = d->cache;
	struct sector *s = NULL;
	v_ilist *pos;
	v_ilist_for_each_rev(pos, &c->lru_sectors) {
		struct sector *test = v_ilist_get(pos, struct sector, linkage);
		if (test->lba < 0)
			continue;
		if (test->lba == lba) {
			s = test;
			break;
		}
	}
	if (s) { // Cache hit
		// Bring it to the front
		v_ilist_remove(&s->linkage);
		v_ilist_prepend(&s->linkage, &c->lru_sectors);
		memcpy(out, s->data, c->sector_size);
		return 0;
	}
	// Cache miss, reuse the least recently used sector
	struct sector *miss = v_ilist_get_first(&c->lru_sectors, struct sector, linkage);
	if (miss->flags & SECTOR_DIRTY) {
		// Need to flush too disk first
		// FIXME: Should check for non-dirty sectors before flushing
		// FIXME: writing isn't implemented yet, panic here.
		panic("s->flags & SECTOR_DIRTY");
	}
	v_ilist_remove(&miss->linkage);
	v_ilist_prepend(&miss->linkage, &c->lru_sectors);
	int ret = flop_block_read(dev, lba, miss->data);
	if (ret) {
		miss->lba = -1;
		return ret;
	}
	miss->lba = lba;
	memcpy(out, miss->data, c->sector_size);
	return 0;
}

static const char *blockdev_names[] = {
	"fd0", "fd1", "fd2", "fd3",
};

static void update_blockdev(struct floppy_drive *d) {
	d->dev.base.ctx = d;
	d->dev.base.name = blockdev_names[d->num];
	d->dev.block_count = flop_block_count;
	d->dev.block_size = flop_block_size;
	d->dev.block_read = sector_cache_read;
	// d->dev.block_write = flop_block_write;
}

static void cmd_sense_interrupt(const char *caller, struct floppy_drive *d) {
	(void)caller;
	int ret = send_byte(d, CMD_SENSE_INTERRUPT);
	if (ret) {
		dbg("%s sense_interrupt: CMD_SENSE_INTERRUPT\n", caller);
		return;
	}
	ret = read_byte(d);
	if (ret < 0) {
		dbg("%s: sense_interrupt read st0 returned %i\n", caller, ret);
		return;
	}
	d->st0 = ret;

	ret = read_byte(d);
	if (ret < 0) {
		dbg("%s: sense_interrupt: read pcn returned %i\n", caller, ret);
		return;
	}
	d->cyl = ret;
}

static int cmd_recalibrate(struct floppy_drive *drive) {
	motor_set(drive, 1);
	for (int i = 0; i < 10; ++i) {
		int ret = send_byte(drive, CMD_RECALIBRATE);
		if (ret) {
			dbg("CMD_RECALIBRATE\n");
			return ret;
		}
		ret = send_byte(drive, drive->num);
		if (ret) {
			dbg("CMD_RECALIBRATE drive num %i byte\n", drive->num);
			return ret;
		}

		ret = wait_irq();
		if (ret) {
			dbg("CMD_RECALIBRATE wait_irq() timed out\n");
			motor_set(drive, 0);
			return ret;
		}

		cmd_sense_interrupt("recalibrate", drive);

		if (drive->st0 & 0xC0) {
			static const char *status_strs[] = {
				0, "error", "invalid", "drive",
			};
			kprintf("recalibrate: status: %s\n", status_strs[drive->st0 >> 6]);
			continue;
		}

		if (!drive->cyl) {
			motor_set(drive, 0);
			return 0;
		}
	}

	kprintf("recalibrate exhausted 10 retries\n");
	motor_set(drive, 0);
	return -1;

	// dbg("drive->num = %i, drive->st0 = %1h\n", drive->num, drive->st0);
	// dbg("0x20 | drive->num = %1h\n", (0x20 | drive->num));

	// int retval = !(drive->st0 == (0x20 | drive->num));
	// dbg("reset_drive() returning %i (%s)\n", retval, retval == 0 ? "OK" : "NOK");
	// return retval;
}

// TODO: Also detect when disk has been taken out and reset this stuff?
static int next_media_type(struct floppy_drive *d) {
	int ret = 0;
	struct drive_params p = drive_types[d->type];
	if (d->f && !p.detect_order[++d->detect_idx]) {
		d->detect_idx = 0;
		ret = -EIO;
	}
	d->f = &floppy_formats[p.detect_order[d->detect_idx]];
	return ret;
}

static int reset_drive(struct floppy_drive *drive) {
	if (!drive->f) {
		int ret = next_media_type(drive);
		if (ret) {
			return ret;
		}
	}
	write_dor(drive, 0x00); // Disable
	write_dor(drive, 0x0C); // Enable
	int ret = wait_irq();
	if (ret) {
		dbg("probe reset on drv %i timed out\n", drive->num);
		return ret;
	}

	// Newer intel manual on the 802078, page 50, says to do this sense + read st0&pcn 4 times.
	// int retries = 3;
	// int eidx = 0;
	// const uint8_t expected[] = { 0xC0, 0xC1, 0xC2, 0xC3 };
	for (int i = 0; i < 1; ++i) {
start:
		ret = send_byte(drive, CMD_SENSE_INTERRUPT);
		if (ret) {
			dbg("CMD_SENSE_INTERRUPT %i\n", i);
			return ret;
		}
		ret = read_byte(drive);
		if (ret < 0) {
			dbg("sense loop iter %i st0 read\n", i);
			return ret;
		}
		uint8_t st0 = ret;
		/*if (st0 != expected[eidx]) {
			if (!--retries) {
				dbg("expected loop retries ret1\n");
				return 1;
			}
			goto start;
		} else {
			eidx++;
		}*/
		ret = read_byte(drive);
		if (ret < 0) {
			dbg("sense loop iter %i cylinder read\n", i);
			return ret;
		}
		uint8_t cylinder = ret;
		dbg("probe sense %i: st0: %1h, cyl: %1h\n", i, st0, cylinder);
	}

	// Can I set this without doing a full reset?
	write_ccr(drive, drive->f->datarate);

	// Configure drive
	ret = send_byte(drive, CMD_SPECIFY);
	if (ret) {
		dbg("CMD_SPECIFY\n");
		return ret;
	}
	struct drive_params p = drive_types[drive->type];
	uint8_t ndma = 0x0;
	ret = send_byte(drive, ((p.step_rate_time << 4) | p.head_unload_time));
	if (ret) {
		dbg("CMD_SPECIFY byte0\n");
		return ret;
	}
	ret = send_byte(drive, ((p.head_load_time << 1) | ndma));
	if (ret) {
		dbg("CMD_SPECIFY byte1\n");
		return ret;
	}

	if (cmd_recalibrate(drive))
		return -1;

	return 0;
}

static int s_debug_probe_run = 0;

static int cmd_seek_internal(struct floppy_drive *d, uint8_t head, uint8_t cyl) {
	if (d->cyl == cyl)
		return 0;
	if (!d->f) {
		int ret = next_media_type(d);
		if (ret)
			return ret;
	}
	if (cyl > d->f->cyls - 1)
		cyl = d->f->cyls - 1;
	motor_set(d, 1);
	for (int i = 0; i < 10; ++i) {
		int ret = send_byte(d, CMD_SEEK);
		if (ret) {
			dbg("CMD_SEEK returned %i\n", ret);
			return ret;
		}
		ret = send_byte(d, (head << 2 )| d->num);
		if (ret) {
			dbg("head+drivenum returned %i\n", ret);
			return ret;
		}
		ret = send_byte(d, cyl);
		if (ret) {
			dbg("cyl returned %i\n", ret);
			return ret;
		}
		ret = wait_irq();
		if (ret) {
			dbg("cmd_seek wait_irq() returned %i\n", ret);
			return ret;
		}
		cmd_sense_interrupt("cmd_seek", d);
		// FIXME: duplicated
		if (d->st0 & 0xC0) {
			static const char *status_strs[] = {
				0, "error", "invalid", "drive",
			};
			dbg("recalibrate: status: %s\n", status_strs[d->st0 >> 6]);
			continue;
		}
		if (d->cyl == cyl) {
			motor_set(d, 0);
			return 0;
		}
		// TODO: also read ID & media type checking here?
	}

	kprintf("cmd_seek exhausted 10 retries\n");
	motor_set(d, 0);
	return -1;
}

static int cmd_seek(struct floppy_drive *d, uint8_t cyl) {
	// TODO: Other seek needed even? Aren't they in sync always?
	int ret = cmd_seek_internal(d, 0, cyl);
	if (ret)
		return ret;
	ret = cmd_seek_internal(d, 1, cyl);
	if (ret)
		return ret;
	return 0;
}

static int handle_cylinder(struct floppy_drive *d, uint8_t cyl, enum dma_dir dir) {
	if (!d->io_base)
		return -1;
	// |MT|MFM|SK|0|
	// MultiTrack, MFM mode.
	// Not setting SK (skip deleted data address mark, not sure what that does yet)
	uint32_t flags = 0xC0;

	uint8_t cmd;
	switch (dir) {
	case dma_dir_read:
		cmd = CMD_READ_DATA | flags;
		break;
	case dma_dir_write:
		cmd = CMD_WRITE_DATA | flags;
		break;
	default:
		assert(NORETURN);
	}

	int ret = cmd_seek(d, cyl);
	if (ret) {
		dbg("handle_cylinder cmd_seek failed\n");
		return ret;
	}

	struct floppy_format *fmt_before = d->f;
	if (!d->f)
		ret = next_media_type(d);

	if (ret)
		return ret;

	for (int i = 0; i < HANDLE_CYLINDER_RETRIES; ++i) {
	retry:
		motor_set(d, 1);
		// unsure if this ccr write is legal here?
		// TODO: Check if this is even needed
		write_ccr(d, d->f->datarate);

		size_t dma_transfer_bytes = (2 * d->f->sectors_per_track) * 512;
		dma_setup(FDC_DMA_CHANNEL, DMA_MODE_SINGLE, dma_buf_phys, dma_transfer_bytes, dir);
		// TODO: minimise this sleep
		sleep(10);

		/*
			page 11 of old doc:
			"Note that the 8272A Read and Write Commands do not
			have implied Seeks. Any R/W command should be
			preceded by: 1) Seek Command; 2) Sense Interrupt
			Status; and 3) Read ID."
		*/
		dbg("trying '%s'\ncmd: %1h, num: %1h, cyl: %1h, head: %1h, sec: %1h, n: %1h, eot: %1h, gpl: %1h, dtl: %1h\n",
		    d->f->name, cmd, d->num, cyl, 0, 1, d->f->sector_size, d->f->sectors_per_track, d->f->gap_len, 0xFF);
		send_byte(d, cmd);
		send_byte(d, d->num); // head (0?) and drive (d->num) (0|0|0|0|0|HEAD|US1|US0|)
		send_byte(d, cyl);                  // C: cylinder
		send_byte(d, 0);                    // H: first head
		send_byte(d, 1);                    // R: record (sector). Set first sector, counting from 1
		send_byte(d, d->f->sector_size);       // N: (n)umber of data bytes in sector. 2 == 512b/sec, I guess?
		send_byte(d, d->f->sectors_per_track); // EOT: end of track, last sector # on track. So effectively sectors/track.
		send_byte(d, d->f->gap_len); // GPL: gap3 length, so length between sectors, excluding VCO sync field.
		send_byte(d, 0xFF); // DTL: data length (unsure, I guess only used if N == 0?)

		ret = wait_irq();
		if (ret) {
			kprintf("handle_cylinder iter %i wait_irq() timed out\n");
			motor_set(d, 0);
			return ret;
		}
		// NOTE: No CMD_SENSE_INTERRUPT needed here

		uint8_t st0, st1, st2;
		uint8_t ret_bps;
		uint8_t ret_cyl, ret_head, ret_sec;
		st0 = read_byte(d);
		st1 = read_byte(d);
		st2 = read_byte(d);
		ret_cyl = read_byte(d);
		ret_head = read_byte(d);
		ret_sec = read_byte(d);
		ret_bps = read_byte(d);

		motor_set(d, 0);

		dbg("st0: %1h st1: %1h st2: %1h cyl: %1h head: %1h sec: %1h bps: %1h\n",
		    st0, st1, st2, ret_cyl, ret_head, ret_sec, ret_bps);

		int error = 0;
		if(st0 & 0xC0) {
            static const char * status[] =
            { 0, "error", "invalid command", "drive not ready" };
            dbg("handle_cylinder: status = %s\n", status[st0 >> 6]);
            error = 1;
        }
        if(st1 & 0x80) {
			// newer doc page 46 table 7.2 says it sets this if "TC" (terminate command? dma Terminal Count?) is not
			// issued after read/write, when trying to access beyond final sector?
			// Also see page 22 section 5.2.5, regarding TC and data transfer termination
			// ==> In my case, I forgot to adjust the transfer size I pass to dma_setup(), so the DMA controller told the FDC to read more data than was available.
            dbg("handle_cylinder: end of cylinder\n");
            error = 1;
        }
        if(st0 & 0x08) {
            dbg("handle_cylinder: drive not ready\n");
            error = 1;
        }
        if(st1 & 0x20) {
            dbg("handle_cylinder: CRC error\n");
            error = 1;
        }
        if(st1 & 0x10) {
            dbg("handle_cylinder: controller timeout\n");
            error = 1;
        }
        if(st1 & 0x04) {
            dbg("handle_cylinder: no data found\n");
            error = 1;
        }
        if((st1|st2) & 0x01) {
            dbg("handle_cylinder: no address mark found\n");
            error = 1;
        }
        if(st2 & 0x40) {
            dbg("handle_cylinder: deleted address mark\n");
            error = 1;
        }
        if(st2 & 0x20) {
            dbg("handle_cylinder: CRC error in data\n");
            error = 1;
        }
        if(st2 & 0x10) {
            dbg("handle_cylinder: wrong cylinder\n");
            error = 1;
        }
        if(st2 & 0x04) {
            dbg("handle_cylinder: uPD765 sector not found\n");
            error = 1;
        }
        if(st2 & 0x02) {
            dbg("handle_cylinder: bad cylinder\n");
            error = 1;
        }
        if(ret_bps != 0x2) {
            dbg("handle_cylinder: wanted 512B/sector, got %d", (1 << (ret_bps + 7)));
            error = 1;
        }
        if(st1 & 0x02) {
            dbg("handle_cylinder: not writable\n");
            error = 2;
        }

        if(!error) {
			if (d->f != fmt_before)
				dbg("floppy: autodetected disk as %s (was %s)\n", d->f->name, fmt_before ? fmt_before->name : "null");
			dma_buf_cyl = cyl;
			dma_buf_drv = d->num;
            return 0;
        }

        if(error > 1) {
            dbg("handle_cylinder: not retrying..\n");
            return -2;
        } else {
			cmd_sense_status("handle_cylinder", d);
			// try next format
			// TODO: doc recommends a separate read_id loop to detect media type, so
			// maybe consider doing that. Now I'm just baking it into this main read routine
			// TODO: I still have no idea how to detect if the disk has changed, so I guess I'll
			// just continue with this loop of trying different media types to find one that works?
			// Would be nicer to react to disk changing and then directly probe the type separately.
			ret = next_media_type(d);
			if (ret)
				continue; // consume a retry, and restart format detect from idx 0

			// goto, so we don't spend retries meant for actual errors
			goto retry;
        }
	}

	kprintf("handle_cylinder exhausted %i retries\n", HANDLE_CYLINDER_RETRIES);
	return -1;
}

int read_cyl(struct floppy_drive *d, uint8_t cyl) {
	return handle_cylinder(d, cyl, dma_dir_read);
}

int write_cyl(struct floppy_drive *d, uint8_t cyl) {
	return handle_cylinder(d, cyl, dma_dir_write);
}

static int probe(v_ma *a) {
	int ret = attach_irq(FDC_IRQ, fdc_irq, "floppy");
	if (ret && !s_debug_probe_run) {
		kprintf("floppy: failed to attach irq %i\n", FDC_IRQ);
		goto fail;
	} else {
		s_debug_probe_run = 1;
	}

	dma_buf_phys = (phys_addr)dma_buf - VIRT_OFFSET;

	kprintf("floppy: dma_buf at %h, dma_buf_phys at %h\n", dma_buf, dma_buf_phys);

	/*
		Right now the only way to be sure what hw there is to check the CMOS.
		If, however, there was a second FDC with more drives, it should be possible
		to use those too. Just init the first two, if available, for now.
	*/
	for (int i = 0; i < FDC_MAX_DRIVES; ++i) {
		enum cmos_fd_type type = cmos_fd_type(i);
		if (!type)
			continue;

		struct floppy_drive *drive = &drives[i];
		drive->type = type;
		drive->num = i;
		drive->name = fd_names[drive->type];
		// Find first suitable FDC
		for (size_t controller = 0; controller < N_FDC_PORTS; ++controller) {
			drive->io_base = fdc_ports[controller];
			if (reset_drive(drive))
				drive->io_base = 0;
			update_blockdev(drive);
			dev_block_register(&drive->dev);
			break;
		}
		if (!drive->io_base)
			continue;
		kprintf("floppy: %s: %s[%i] detected at %2h\n", blockdev_names[i], fd_names[type], type, drives[i].io_base);
	}

	return 0;
fail:
	return ret;
}

/*
	Debug menu stuff below, for manually running different commands with
	lots of debug output. Press 'P' to enter the menu from console.
*/

static int dump_fd_types(void *ctx) {
	(void)ctx;
	enum cmos_fd_type type = cmos_fd_type(CMOS_FD_A);
	if (type)
		kprintf("cmos says A is %s (%h), storing to a.\n", fd_names[type], type);
	type = cmos_fd_type(CMOS_FD_B);
	if (type)
		kprintf("cmos says B is %s (%h), storing to b.\n", fd_names[type], type);
	return 0;
}

static int dump_flop_irqs(void *ctx) {
	(void)ctx;
	kprintf("floppy IRQs: %i\n", irq_counts[FDC_IRQ]);
	return 0;
}

static int clear_terminal(void *ctx) {
	(void)ctx;
	terminal_clear();
	return 0;
}

// FIXME: should probe() funcs in drivers be built with the assumption
// they may be re-run?
static int rerun_probe(void *ctx) {
	(void)ctx;
	return probe(NULL);
}

int cmd_sense(struct floppy_drive *d) {
	int ret = send_byte(d, CMD_SENSE_DRIVE_STATUS);
	if (ret) {
		dbg("CMD_SENSE_DRIVE_STATUS returned %i\n", ret);
		return ret;
	}
	uint8_t head = 0x0;
	ret = send_byte(d, (head << 2 )| d->num);
	if (ret) {
		dbg("cmd_sense head+drivenum returned %i\n", ret);
		return ret;
	}
	ret = wait_irq();
	if (ret) {
		dbg("cmd_sense wait_irq failed\n");
		return ret;
	}
	ret = read_byte(d);
	if (ret < 0) {
		dbg("cmd_sense read_byte() returned %i\n", ret);
		return ret;
	}
	uint8_t st3 = ret;
	dbg("cmd_sense(%i) = %u\n", d->num, st3);
	ret = wait_for_ready_write(d);
	if (ret) {
		dbg("cmd_sense wait write timeout\n");
		return ret;
	}
	return 0;
}

static void hexdump(uint8_t *data, size_t bytes) {
	int tmp = 0;
	for (size_t i = 0; i < bytes; ++i) {
		kprintf("%0h ", data[i]);
		++tmp;
		if (tmp == 8)
			kput(' ');
		if (tmp == 16) {
			kput('\n');
			tmp = 0;
		}
	}
}

static int seek_selected(void *ctx) {
	(void)ctx;
	struct floppy_drive *d = &drives[s_cur_drive];
	int ret = cmd_seek(d, selected_cyl);
	dbg("cmd_seek(%s, %i) -> %i (d->cyl: %i)\n", s_cur_drive ? "B" : "A", selected_cyl, ret, d->cyl);
	return ret;
}

static int cyl_add(void *ctx) {
	uint8_t *cyl = ctx;
	if (*cyl >= 80)
		return 0;
	kprintf("cyl_to_read = %1h\n", ++*cyl);
	return 0;
}

static int cyl_sub(void *ctx) {
	uint8_t *cyl = ctx;
	if (*cyl <= 0)
		return 0;
	kprintf("cyl_to_read = %1h\n", --*cyl);
	return 0;
}

static int hexdump_cyl(void *ctx) {
	(void)ctx;
	memset(dma_buf, 0x00, DMA_BUF_SIZE);
	dma_buf_cyl = -1;
	struct floppy_drive *d = &drives[s_cur_drive];
	int ret = read_cyl(d, selected_cyl);
	dbg("read_cyl(%s, %i) -> %i\n", s_cur_drive ? "B" : "A", selected_cyl, ret);
	if (ret)
		return ret;

	hexdump(dma_buf, DMA_BUF_SIZE);
	return 0;
}

static int hash_cyl(void *ctx) {
	(void)ctx;
	memset(dma_buf, 0x00, DMA_BUF_SIZE);
	dma_buf_cyl = -1;
	struct floppy_drive *d = &drives[s_cur_drive];
	int ret = read_cyl(d, selected_cyl);
	dbg("read_cyl(%s, %i) -> %i\n", s_cur_drive ? "B" : "A", selected_cyl, ret);
	if (ret)
		return ret;

	v_hash hash = v_hash_init();
	v_hash_bytes(&hash, dma_buf, DMA_BUF_SIZE);
	kprintf("FNV hash: %h\n", hash);
	return 0;
}

static int toggle_verbose(void *ctx) {
	(void)ctx;
	s_verbose = !s_verbose;
	kprintf("verbose %s\n", s_verbose ? "enabled" : "disabled");
	return 0;
}

static int toggle_drive(void *ctx) {
	(void)ctx;
	s_cur_drive = !s_cur_drive;
	kprintf("drive is now %s\n", s_cur_drive ? "B" : "A");
	return 0;
}

struct cmd_list fd_debug = {
	.name = "fd_debug",
	.cmds = {
		{ {}, 0, -1, NULL,            TASK(dump_fd_types),  "show cmos fd types",             't', 0 },
		{ {}, 0, -1, NULL,            TASK(dump_flop_irqs),  "dump flop irqs",                'i', 0 },
		{ {}, 0, -1, NULL,            TASK(clear_terminal),  "clear screen",                  'x', 0 },
		// { {}, 0, -1, NULL,            TASK(rerun_probe),  "rerun probe",                      'r', 0 },
		{ {}, 0,  1, NULL,            TASK(seek_selected),  "seek drive to selected_cyl",     's', 0 },
		{ {}, 0,  1, NULL,            TASK(hexdump_cyl),  "hexdump selected_cyl on drive",    'k', 0 },
		{ {}, 0,  1, NULL,            TASK(hash_cyl),  "hash selected_cyl on drive",           ',', 0 },
		{ {}, 0,  1, &selected_cyl,    TASK(cyl_add),  "increment cylinder",                   'm', 0 },
		{ {}, 0,  1, &selected_cyl,    TASK(cyl_sub),  "decrement cylinder",                   'n', 0 },
		{ {}, 0,  1, NULL,            TASK(toggle_verbose),  "toggle verbose output",          'v', 0 },
		{ {}, 0,  1, NULL,            TASK(toggle_drive),  "toggle drive",                    ' ', 0 },
		{ 0 },
	}
};

struct driver floppy = {
	.name = "floppy",
	.probe = probe,
	.deps = {
		"cmos",
		NULL,
	}
};

register_driver(floppy);
