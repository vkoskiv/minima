/* The bootloader will look at this image and start execution at the symbol
   designated as the entry point. */
ENTRY(_start)
 
/* Tell where the various sections of the object files will be put in the final
   kernel image. */
SECTIONS
{
	. = 0x00000000;
	_address_space_start = .;

	. = 0x00010000;
	.stage0 ALIGN(4K) : AT (ADDR (.stage0))
	{
		_start.o
		mman.o
		paging.o
	}

	. += 0xC0000000;
	_kernel_virtual_start = .;
	_kernel_physical_start = . - 0xC0000000;
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

	_kernel_virtual_end = .;
	_kernel_physical_end = . - 0xC0000000;
}
