/*
	Expose symbols from linker.ld to C land.
*/

#include <mm/types.h>

extern void _address_space_start(void);
#define address_space_start ((phys_addr)&_address_space_start)
extern void _kernel_physical_start(void);
#define kernel_physical_start ((phys_addr)&_kernel_physical_start)
extern void _kernel_physical_end(void);
#define kernel_physical_end ((phys_addr)&_kernel_physical_end)
extern void _initcalls_start(void);
#define initcalls_start ((virt_addr)&_initcalls_start)
extern void _initcalls_end(void);
#define initcalls_end ((virt_addr)&_initcalls_end)
extern void _elf_hdr_with_padding_start(void);
#define elf_hdr_with_padding_start ((phys_addr)&_elf_hdr_with_padding_start)
extern void _elf_hdr_with_padding_end(void);
#define elf_hdr_with_padding_end ((phys_addr)&_elf_hdr_with_padding_end)

#define STAGE0_PD_ADDR 0x1000
#define STAGE0_PT1_ADDR 0x2000
#define STAGE0_PT2_ADDR 0x3000
