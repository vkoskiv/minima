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

#if DEBUG_FLOPPY == 1
#define dbg(...) kprintf(__VA_ARGS__)
#else
#define dbg(...)
#endif

#define MAX_DRIVES 2

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

/*
gcc 9 -O0
push ebp;
mov ebp, esp;
mov eax, [floppy_interrupts];
add eax, 1;
mov [floppy_interrupts], eax;
nop;
pop;
ret;
*/

// 8 insn in c (frame ptr)
static void fdc_irq(struct irq_regs regs) {
	(void)regs;
	++floppy_interrupts;
}


/*
	inc dword ptr [floppy_interrupts];
	ret;
*/
// void fdc_irq(void);
// asm(
// ".extern floppy_interrupts\n"
// ".globl fdc_irq\n"
// "fdc_irq:"
// "	inc dword ptr [floppy_interrupts];"
// "	ret;"
// );

static const uint32_t irq_wait_timeout = (1024*1024);

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

struct floppy_drive {
	enum cmos_fd_type type;
	const char *name;
	uint16_t io_base;
	uint8_t num;
	uint8_t cyl;
	uint8_t st0;
};

static struct floppy_drive drives[MAX_DRIVES] = { 0 };

uint16_t fdc_ports[] = {
	0x3F0, // Drives A & B, or fd0, fd1
	0x370, // If a system has >2 drives, drives fd2, fd3
	0x360, // Quite rare, but would be fun to try?
};
#define N_FDC_PORTS (sizeof(fdc_ports) / sizeof(fdc_ports[0]))

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

static int probe(v_ma *a);
struct driver floppy = {
	.name = "floppy",
	.probe = probe,
	.deps = {
		"cmos",
		NULL,
	}
};

/*
	page 11 of old doc:
	"Note that the 8272A Read and Write Commands do not
have implied Seeks. Any R/W command should be
preceded by: 1) Seek Command; 2) Sense Interrupt
Status; and 3) Read ID."
*/
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

// r/w 'Digital Output Reg.'     |RSVD  |RSVD |MOTEN1|MOTEN0|DMAGATE#|RESET# | RSVD  |DRVSEL |
static void motor_set(struct floppy_drive *d, int on) {
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
}

static void configure_drive(struct floppy_drive *d);

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

#define IO_OFF_CCR   0x07 // w

void write_ccr(struct floppy_drive *d, uint8_t byte) {
	io_out8(d->io_base + IO_OFF_CCR, byte);
}
/*
	This function actually kind of sometimes works.
*/
static int reset_drive(struct floppy_drive *drive) {
	assert(read_eflags() & EFLAGS_IF);

	// not needed?
	write_ccr(drive, 0);

	write_dor(drive, 0x00); // Disable
	write_dor(drive, 0x0C); // Enable
	int ret = wait_irq();
	if (ret) {
		dbg("probe reset on drv %i timed out\n", drive->num);
		return ret;
	}
	// Newer intel manual on the 802078, page 50, says to do this sense + read st0&pcn 4 times.
	int retries = 3;
	int eidx = 0;
	const uint8_t expected[] = { 0xC0, 0xC1, 0xC2, 0xC3 };
	for (int i = 0; i < 4; ++i) {
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
		if (st0 != expected[eidx]) {
			if (!--retries) {
				dbg("expected loop retries ret1\n");
				return 1;
			}
			goto start;
		} else {
			eidx++;
		}
		ret = read_byte(drive);
		if (ret < 0) {
			dbg("sense loop iter %i cylinder read\n", i);
			return ret;
		}
		uint8_t cylinder = ret;
		dbg("probe sense %i: st0: %1h, cyl: %1h\n", i, st0, cylinder);
	}
	// Configure drive
	ret = send_byte(drive, CMD_SPECIFY);
	if (ret) {
		dbg("CMD_SPECIFY\n");
		return ret;
	}
	struct drive_params p = floppy_params[drive->type];
	uint8_t ndma = 0x1;
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

	// Recalibrate
	motor_set(drive, 1);
	ret = send_byte(drive, CMD_RECALIBRATE);
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

	// Check status
	ret = send_byte(drive, CMD_SENSE_INTERRUPT);
	if (ret) {
		dbg("post-recalibrate CMD_SENSE_INTERRUPT\n");
		return ret;
	}
	ret = read_byte(drive);
	if (ret < 0) {
		dbg("post-recalibrate st0\n");
		return ret;
	}
	drive->st0 = ret;
	ret = read_byte(drive);
	if (ret < 0) {
		dbg("post-recalibrate pcn\n");
		return ret;
	}
	drive->cyl = ret;

	motor_set(drive, 0);

	dbg("drive->num = %i, drive->st0 = %1h\n", drive->num, drive->st0);
	dbg("0x20 | drive->num = %1h\n", (0x20 | drive->num));

	int retval = !(drive->st0 == (0x20 | drive->num));
	dbg("reset_drive() returning %i (%s)\n", retval, retval == 0 ? "OK" : "NOK");
	return retval;
}

static int s_debug_probe_run = 0;

static int probe(v_ma *a) {
	int ret = attach_irq(IRQ0_OFFSET + IRQ_FDC, fdc_irq, "floppy");
	if (ret && !s_debug_probe_run) {
		kprintf("floppy: failed to attach irq %i\n", IRQ0_OFFSET + IRQ_FDC);
		goto fail;
	} else {
		s_debug_probe_run = 1;
	}
	/*
		Right now the only way to be sure what hw there is to check the CMOS.
		If, however, there was a second FDC with more drives, it should be possible
		to use those too. Just init the first two, if available, for now.
	*/
	int fdc_found = 0;
	size_t controller = 0;
	for (int i = 0; i < MAX_DRIVES; ++i) {
		enum cmos_fd_type type = cmos_fd_type(i);
		if (!type)
			continue;

		struct floppy_drive *drive = &drives[i];
		drive->type = type;
		drive->num = i;
		if (fdc_found) {
			drive->io_base = fdc_ports[controller];
			dbg("Reusing IO base %3h for drive %i\n", drive->io_base, drive->num);
		} else {
			// Find first suitable FDC
			for (controller = 0; controller < N_FDC_PORTS; ++controller) {
				drive->io_base = fdc_ports[controller];
				/*
					For reasons I can't fathom, we can only invoke this reset once per FDC?
					Even though the reset dance takes drive-specific parameters?
					Just skip reset_drive for drive b, and assume it's there if CMOS
					says so, I guess.
				*/
				if (reset_drive(drive)) {
					drive->io_base = 0;
					break;
				} else {
					fdc_found = 1;
				}
				break;
			}
		}
		if (!drive->io_base)
			continue;
		drive->name = fd_names[drive->type];
		kprintf("floppy: cmos fd%i: %s(%i) detected at %3h\n", i, fd_names[type], type, drives[i].io_base);
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
	enum cmos_fd_type type = cmos_fd_type(CMOS_FD_A);
	if (type)
		kprintf("cmos says A is %s (%h), storing to a.\n", fd_names[type], type);
	type = cmos_fd_type(CMOS_FD_B);
	if (type)
		kprintf("cmos says B is %s (%h), storing to b.\n", fd_names[type], type);
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

int rerun_probe(void *ctx) {
	(void)ctx;
	return probe(NULL);
}

int cmd_seek(struct floppy_drive *d, uint8_t cyl) {
	int ret = send_byte(d, CMD_SEEK);
	if (ret) {
		dbg("CMD_SEEK returned %i\n", ret);
		return ret;
	}
	uint8_t head = 0x0;
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
	return 0;
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
int seek_40(void *ctx) {
	int ret = cmd_seek(&drives[0], 40);
	dbg("cmd_seek -> %i\n", ret);
	return ret;
}

int seek_0(void *ctx) {
	int ret = cmd_seek(&drives[0], 0);
	dbg("cmd_seek -> %i\n", ret);
	return ret;
}

/*
	Reads ST3:
	|Fault|WP|RDY|T0|TS|HD|US1|US0|

	TS = Two Side
	HD = side select, head address
*/
static void dump_st3(uint8_t st3) {
	if (st3 & (1 << 7))
		dbg("FAULT ");
	if (st3 & (1 << 6))
		dbg("WP ");
	if (st3 & (1 << 5))
		dbg("RDY ");
	if (st3 & (1 << 4))
		dbg("TRK0 ");
	if (st3 & (1 << 3))
		dbg("TS ");
	if (st3 & (1 << 2))
		dbg("HeadAddr ");
	if (st3 & (1 << 1))
		dbg("US1 ");
	if (st3 & (1 << 0))
		dbg("US0 ");
	dbg("\n");
}

int sense(void *ctx) {
	(void)ctx;
	int ret = cmd_sense(&drives[0]);
	dbg("cmd_sense() -> %1h:\n\t", ret);
	dump_st3(ret);
	return ret;
}
struct cmd_list fd_debug = {
	.name = "fd_debug",
	.cmds = {
		{ {}, 0, -1, NULL,      TASK(dump_fd_types),  "show cmos fd types",  't', 0 },
		{ {}, 0, -1, NULL,      TASK(dump_flop_irqs),  "dump flop irqs",               'i', 0 },
		{ {}, 0, -1, NULL,      TASK(clear_terminal),  "clear screen",               'x', 0 },
		{ {}, 0, -1, NULL,      TASK(rerun_probe),  "rerun probe",               'r', 0 },
		{ {}, 0, -1, NULL,      TASK(seek_40),  "cmd_seek(40)",               's', 0 },
		{ {}, 0, -1, NULL,      TASK(seek_0),  "cmd_seek(0)",               'd', 0 },
		{ {}, 0, -1, NULL,      TASK(sense),  "cmd_sense",               'v', 0 },
		{ {}, 0, -1, NULL,      TASK(increase_fiforetries),  "fifo_retries += 1000", 'h', 0 },
		{ {}, 0, -1, NULL,      TASK(decrease_fiforetries),  "fifo_retries -= 1000", 'n', 0 },
		{ 0 },
	}
};

register_driver(floppy);
