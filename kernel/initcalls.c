#include <initcalls.h>
#include <linker.h>

// See linker.ld for more of how this works. TL;DR is, symbols marked with
// __attribute__((section(".initcalls"))) end up in that section, and the
// linker script creates symbols at the beginning and end of those symbols, which
// allows us to iterate the symbols like an array, seen here below.

void run_initcalls(void) {
	int n_initcalls = (initcalls_end - initcalls_start) / sizeof(virt_addr);
	void (**initcalls)(void) = (void (**)(void))initcalls_start;
	for (int i = 0; i < n_initcalls; ++i)
		initcalls[i]();
}

