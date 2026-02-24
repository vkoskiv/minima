#ifndef _CMOS_H_
#define _CMOS_H_

void cmos_disable_nmi(void);
void cmos_enable_nmi(void);

enum cmos_fd_type {
	cmos_fd_none = 0,
	cmos_fd_525_360,
	cmos_fd_525_1200,
	cmos_fd_35_720,
	cmos_fd_35_1440,
	cmos_fd_35_2880,
};

#define CMOS_FD_A 0x0
#define CMOS_FD_B 0x1

extern const char *fd_names[cmos_fd_35_2880 + 1];
enum cmos_fd_type cmos_fd_type(unsigned char drive);

#endif
