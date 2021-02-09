/* Declare constants for the multiboot header. */
.set ALIGN,    1<<0             /* align loaded modules on page boundaries */
.set MEMINFO,  1<<1             /* provide memory map */
.set FLAGS,    ALIGN | MEMINFO  /* this is the Multiboot 'flag' field */
.set MAGIC,    0x1BADB002       /* 'magic number' lets bootloader find the header */
.set CHECKSUM, -(MAGIC + FLAGS) /* checksum of above, to prove we are multiboot */
 
/* 
Declare a multiboot header that marks the program as a kernel. These are magic
values that are documented in the multiboot standard. The bootloader will
search for this signature in the first 8 KiB of the kernel file, aligned at a
32-bit boundary. The signature is in its own section so the header can be
forced to be within the first 8 KiB of the kernel file.
*/
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
 
/*
The multiboot standard does not define the value of the stack pointer register
(esp) and it is up to the kernel to provide a stack. This allocates room for a
small stack by creating a symbol at the bottom of it, then allocating 16384
bytes for it, and finally creating a symbol at the top. The stack grows
downwards on x86. The stack is in its own section so it can be marked nobits,
which means the kernel file is smaller because it does not contain an
uninitialized stack. The stack on x86 must be 16-byte aligned according to the
System V ABI standard and de-facto extensions. The compiler will assume the
stack is properly aligned and failure to align the stack will result in
undefined behavior.
*/
.section .bootstrap_stack, "aw", @nobits
stack_bottom:
.skip 16384 # 16 KiB
stack_top:
 
/*
Preallocate pages for paging
*/
.section .bss, "aw", @nobits
	.align 4096
.global boot_page_directory
boot_page_directory:
	.skip 4096
.global boot_page_table1
boot_page_table1:
	.skip 4096

.section .text
.global _start
.type _start, @function
_start:
	/* Get physical address of boot_page_table1*/
	movl $(boot_page_table1 - 0xC0000000), %edi
	movl $0, %esi
	/* Map 1023 pages, 1024th is the VGA text buffer */
	movl $1023, %ecx

1:
	cmpl $(_address_space_start), %esi
	/* cmpl $(_kernel_physical_start), %esi */
	jl 2f
	cmpl $(_kernel_physical_end), %esi
	jge 3f

	movl %esi, %edx
	orl $0x003, %edx
	movl %edx, (%edi)

2:
	addl $4096, %esi
	addl $4, %edi
	loop 1b

3:
	/* Map VGA memory */
	movl $(0x000B8000 | 0x003), boot_page_table1 - 0xC0000000 + 1023 * 4

	/* Set up the page directory */
	movl $(boot_page_table1 - 0xC0000000 + 0x003), boot_page_directory - 0xC0000000 + 0
	movl $(boot_page_table1 - 0xC0000000 + 0x003), boot_page_directory - 0xC0000000 + 768 * 4

	/* Map the last PDE to the page directory base so we can modify the PD*/
	movl $(boot_page_directory - 0xC0000000), boot_page_directory - 0xC0000000 + 1023 * 4

	/* Set cr3 to page directory physical address */
	movl $(boot_page_directory - 0xC0000000), %ecx
	movl %ecx, %cr3

	/* Enable paging and write-protect bit */
	movl %cr0, %ecx
	orl $0x80010000, %ecx
	movl %ecx, %cr0

	/* Absolute jump to higher half */
	lea 4f, %ecx
	jmp *%ecx

.section .text

4:
	/* Paging is set up and enabled now. */
	/* Discard the identity mapping */
	/* movl $0, boot_page_directory + 0 */

	/* Reload cr3 to force a TLB flush to apply changes */
	/* movl %cr3, %ecx */
	/* movl %ecx, %cr3 */

	/*
	To set up a stack, we set the esp register to point to the top of the
	stack (as it grows downwards on x86 systems). This is necessarily done
	in assembly as languages such as C cannot function without a stack.
	*/
	movl $stack_top, %esp
 
	/*
	This is a good place to initialize crucial processor state before the
	high-level kernel is entered. It's best to minimize the early
	environment where crucial features are offline. Note that the
	processor is not fully initialized yet: Features such as floating
	point instructions and instruction set extensions are not initialized
	yet. The GDT should be loaded here. Paging should be enabled here.
	C++ features such as global constructors and exceptions will require
	runtime support to work as well.
	*/

	/*
	Enter the high-level kernel. The ABI requires the stack is 16-byte
	aligned at the time of the call instruction (which afterwards pushes
	the return pointer of size 4 bytes). The stack was originally 16-byte
	aligned above and we've pushed a multiple of 16 bytes to the
	stack since (pushed 0 bytes so far), so the alignment has thus been
	preserved and the call is well defined.
	*/
	add $0xC0000000, %ebx
	push %ebx
	push %eax
	call kernel_main
 
	/*
	If the system has nothing more to do, put the computer into an
	infinite loop. To do that:
	1) Disable interrupts with cli (clear interrupt enable in eflags).
	   They are already disabled by the bootloader, so this is not needed.
	   Mind that you might later enable interrupts and return from
	   kernel_main (which is sort of nonsensical to do).
	2) Wait for the next interrupt to arrive with hlt (halt instruction).
	   Since they are disabled, this will lock up the computer.
	3) Jump to the hlt instruction if it ever wakes up due to a
	   non-maskable interrupt occurring or due to system management mode.
	*/
	cli
1:	hlt
	jmp 1b
.size _start, . - _start

.global discard_identity
.type discard_identity, @function
discard_identity:
	movl $0, boot_page_directory + 0
	movl %cr3, %ecx
	movl %ecx, %cr3
	ret
