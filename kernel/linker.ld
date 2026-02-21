SECTIONS
{
	. = 0x00000000;
	_address_space_start = .;

	/*
		The ELF header is actually only 52 bytes, but the first
		section '.stage0' in linker.ld is PAGE_SIZE aligned. So
		this bloats up the kernel image by PAGE_SIZE.
		TODO: Once ELF header is no longer needed, free the page
		it resides in.
	*/
	. = KERNEL_PHYS_ADDR + ELF_HDR_SIZE;
	_kernel_physical_start = .;
	ENTRY(_stage0_init)
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
	}

	.initcalls ALIGN(4) : AT (ADDR (.initcalls))
	{
		_initcalls_start = .;
		*(.initcalls)
		_initcalls_end = .;
	}

	_kernel_physical_end = . - VIRT_OFFSET;
}
