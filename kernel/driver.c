#include <driver.h>
#include <stddef.h>
#include <kprintf.h>
#include <utils.h>

V_ILIST(g_drivers);
V_ILIST(g_drivers_initialized);
V_ILIST(g_drivers_failed);

static struct driver *find(const char *name, v_ilist *l) {
	v_ilist *pos;
	v_ilist_for_each(pos, l) {
		struct driver *d = v_ilist_get(pos, struct driver, linkage);
		if (strcmp(d->name, name) == 0)
			return d;
	}
	return NULL;
}

int deps_loaded(struct driver *d) {
	size_t idx = 0;
	const char *depname;
	while ((depname = d->deps[idx++])) {
		struct driver *dep = find(depname, &g_drivers_initialized);
		if (!dep)
			return find(depname, &g_drivers)->on_demand = 0;
	}
	return 1;
}

void driver_init(v_ma *a) {
	v_ilist *pos, *temp;
	v_ilist_for_each_safe(pos, temp, &g_drivers) {
		struct driver *d = v_ilist_get(pos, struct driver, linkage);
		v_ilist_remove(&d->linkage);
		if (d->on_demand || !deps_loaded(d)) {
			v_ilist_prepend(&d->linkage, &g_drivers); // postpone
			continue;
		}
		int ret = d->probe(a);
		if (ret) {
			kprintf("Failed to load driver '%s'\n", d->name);
			v_ilist_prepend(&d->linkage, &g_drivers_failed);
		} else {
			v_ilist_prepend(&d->linkage, &g_drivers_initialized);
		}
	}
	v_ilist_for_each_safe(pos, temp, &g_drivers) {
		struct driver *d = v_ilist_get(pos, struct driver, linkage);
		v_ilist_remove(&d->linkage);
		if (d->on_demand)
			continue;
		kprintf("Failed to load driver '%s', missing dependencies: ", d->name);
		size_t idx = 0;
		const char *depname;
		while ((depname = d->deps[idx++])) {
			struct driver *dep = find(depname, &g_drivers_failed);
			if (dep)
				kprintf("%s ", dep->name);
		}
		v_ilist_prepend(&d->linkage, &g_drivers_failed);
		kput('\n');
	}
}
