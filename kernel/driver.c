#include "driver.h"
#include <stddef.h>

V_ILIST(g_drivers);

void driver_init(v_ma *a) {
	v_ilist *pos;
	v_ilist_for_each(pos, &g_drivers) {
		struct driver *d = v_ilist_get(pos, struct driver, linkage);
		d->probe(a);
	}
}
