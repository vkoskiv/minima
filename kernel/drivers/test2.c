#include <driver.h>
#include <kprintf.h>

static int probe(v_ma *a) {
	(void)a;
	kprintf("%s: probe()\n", __FILE__);
	return 0;
}

struct driver test2 = {
	.name = "test2",
	.probe = probe,
	.on_demand = 1,
	.deps = { NULL }
};

register_driver(test2);
