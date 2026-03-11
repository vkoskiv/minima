#include <initcalls.h>
#include <linker.h>

void run_initcalls(void) {
	int n_initcalls = (initcalls_end - initcalls_start) / sizeof(virt_addr);
	void (**initcalls)(void) = (void (**)(void))initcalls_start;
	for (int i = 0; i < n_initcalls; ++i)
		initcalls[i]();
}

