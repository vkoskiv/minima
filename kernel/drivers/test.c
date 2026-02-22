#include <driver.h>
#include <kprintf.h>

static void probe(v_ma *a) {
	(void)a;
	kprintf("%s: probe() called\n", __FILE__);
}

struct driver test = {
	.name = "test",
	.probe = probe,
};

register_driver(test);
