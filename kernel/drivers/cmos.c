#include <driver.h>
#include <io.h>
#include <drivers/cmos.h>
#include <assert.h>

#define IO_WAIT io_wait()

#define CMOS_PORT_REG  0x70
#define CMOS_PORT_DOUT 0x71

#define CMOS_REG_RTC_SECONDS      (0x00) // 0-59
#define CMOS_REG_RTC_MINUTES      (0x02) // 0-59
#define CMOS_REG_RTC_HOURS        (0x04) // 0-23 or 1-12 & hi bit true == pm
#define CMOS_REG_RTC_WEEKDAY      (0x06) // 1-7, 1 == Sunday
#define CMOS_REG_RTC_DAY_OF_MONTH (0x07) // 1-31
#define CMOS_REG_RTC_MONTH        (0x08) // 1-12
#define CMOS_REG_RTC_YEAR         (0x09) // 0-99
#define CMOS_REG_RTC_CENTURY      (0x32) // 19-20
#define CMOS_REG_RTC_STATUS_A     (0x0A)
#define CMOS_REG_RTC_STATUS_B     (0x0B)
#define CMOS_REG_FD_INFO          (0x10)

static uint8_t s_nmi_disabled;
static uint8_t s_cmos_initialized = 0;

#define CMOS_CACHED_FDTYPE (0x1 << 0)
#define CMOS_CACHED_RTC    (0x1 << 1)
uint8_t s_cmos_cached;

/*
	https://en.wikipedia.org/wiki/Determination_of_the_day_of_the_week#Sakamoto's_methods
*/
static int day_of_week(uint16_t y, uint8_t m, uint8_t d) {
	static int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2,4 };
	if (m < 3)
		y -= 1;
	return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static uint8_t cmos_read(uint8_t reg) {
	io_out8(CMOS_PORT_REG, (s_nmi_disabled << 7) | (reg));
	IO_WAIT;
	return io_in8(CMOS_PORT_DOUT);
}

static void cmos_init(void) {
	s_nmi_disabled = 0;
	s_cmos_cached = 0;
	s_cmos_initialized = 1;
}

void cmos_disable_nmi(void) {
	assert(!s_nmi_disabled);
	s_nmi_disabled = 1;
}

void cmos_enable_nmi(void) {
	assert(s_nmi_disabled);
	s_nmi_disabled = 0;
}

const char *fd_names[cmos_fd_35_2880 + 1] = {
	[cmos_fd_none]     = "N/A",
	[cmos_fd_525_360]  = "5.25\" 360K",
	[cmos_fd_525_1200] = "5.25\" 1.2M",
	[cmos_fd_35_720]   = "3.5\" 720K",
	[cmos_fd_35_1440]  = "3.5\" 1.44M",
	[cmos_fd_35_2880]  = "3.5\" 2.88M",
};

uint8_t s_cmos_fdtype;
enum cmos_fd_type cmos_fd_type(unsigned char drive) {
	assert(s_cmos_initialized);
	if (drive > CMOS_FD_B)
		return cmos_fd_none;
	if (!(s_cmos_cached & CMOS_CACHED_FDTYPE)) {
		s_cmos_fdtype = cmos_read(CMOS_REG_FD_INFO);
		s_cmos_cached |= CMOS_CACHED_FDTYPE;
	}
	return drive ? (s_cmos_fdtype & 0x0F) : ((s_cmos_fdtype & 0xF0) >> 4);
}

static int probe(v_ma *a) {
	cmos_init();
	return 0;
}

struct driver cmos = {
	.name = "cmos",
	.probe = probe,
	.on_demand = 1,
	.deps = { NULL },
};

register_driver(cmos);
