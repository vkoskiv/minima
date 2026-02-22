#include <stdbool.h>
#include <stddef.h>
#include <vkern.h>
#include <stdint.h>
#include <kprintf.h>
#include <idt.h>
#include <mman.h>
#include <keyboard.h>
#include <serial_debug.h>
#include <timer.h>
#include <pfa.h>
#include <sched.h>
#include <assert.h>
#include <x86.h>
#include <initcalls.h>
#include <driver.h>
 
#if defined(__linux__)
	#error "Cross compiler required, see toolchain/buildtoolchain.sh"
#endif
 
#if !defined(__i386__)
	#error "ix86-elf compiler required"
#endif

extern void pit_initialize(void);

// In console.c
extern int console_task(void *);

void sched_initial(void);

#define KERNEL_ARENA_PAGES 4

void stage1_init(void) {
	terminal_init(VGA_WIDTH, VGA_HEIGHT);
	kbd_init();
	serial_setup();
	cli();
	idt_init();
	gdt_init();
	pit_initialize();
	pfa_init();
	mman_init();
	run_initcalls();

	uint8_t *k_arena_buf = kmalloc(KERNEL_ARENA_PAGES * PAGE_SIZE);
	v_ma k_arena = v_ma_from_buf(k_arena_buf, KERNEL_ARENA_PAGES * PAGE_SIZE);

	driver_init(&k_arena);
	sched_init();

	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	page_directory[0] = 0x2;
	flush_cr3();
	uint32_t kernel_bytes = kernel_physical_end - kernel_physical_start;
	kprintf("Kernel image at %h-%h (%ik, %i pages)\n", kernel_physical_start, kernel_physical_end,
	        kernel_bytes / 1024, PAGE_ROUND_UP(kernel_bytes) / PAGE_SIZE);

	task_create(console_task, NULL, "console_task", 0);

	assert(!(read_eflags() & EFLAGS_IF));
	sched_initial();
	assert(NORETURN);
}
