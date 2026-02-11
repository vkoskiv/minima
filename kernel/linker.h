/*
	Expose symbols from linker.ld to C land.
*/
extern void _address_space_start(void);
#define address_space_start ((phys_addr)&_address_space_start)
extern void _kernel_physical_start(void);
#define kernel_physical_start ((phys_addr)&_kernel_physical_start)
extern void _kernel_physical_end(void);
#define kernel_physical_end ((phys_addr)&_kernel_physical_end)

