//
// drv.h - renamed from original driver.h to prevent annoying clash
// with bash autocompletion when navigating to include/driver/
//

#ifndef _DRV_H_
#define _DRV_H_

#include <initcalls.h>
#include <vkern.h>

struct driver {
	const char *name;
	int (*probe)(v_ma *);
	void *driver_state;
	v_ilist linkage;
	int on_demand;
	const char *deps[];
};

void driver_init(v_ma *a);

extern v_ilist g_drivers;

#define register_driver(driver) \
static void _register_##driver(void) { \
	driver.linkage = V_ILIST_INIT(driver.linkage); \
	v_ilist_prepend(&driver.linkage, &g_drivers); \
} \
add_initcall(_register_##driver);


#endif
