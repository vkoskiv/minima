/* The bootloader will look at this image and start execution at the symbol
   designated as the entry point. */
ENTRY(_stage0_init)
 
/* Tell where the various sections of the object files will be put in the final
   kernel image. */
SECTIONS
{
	. = 0x00000000;
	_address_space_start = .;

	. = KERNEL_PHYS_ADDR;
	_kernel_physical_start = .;
	.stage0 ALIGN(4K) : AT (ADDR (.stage0))
	{
		stage0.o
		stage0_asm.o
	}

	. += VIRT_OFFSET;
	.text ALIGN(4K) : AT (ADDR (.text))
	{
		*(.text)
	}
 
	/* Read-only data. */
	.rodata ALIGN(4K) : AT (ADDR (.rodata))
	{
		*(.rodata)
	}

	.data ALIGN(4K) : AT (ADDR (.data))
	{
		*(.data)
	}
 
	/* Read-write data (uninitialized) and stack */
	.bss ALIGN(4K) : AT (ADDR (.bss))
	{
		*(COMMON)
		*(.bss)
		*(.bootstrap_stack)
	}

	_kernel_physical_end = . - VIRT_OFFSET;
}
