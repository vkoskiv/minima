#include <stdbool.h>
#include <stddef.h>
#include <vkern.h>
#include <stdint.h>
#include <kprintf.h>
#include <idt.h>
#include <mm/vma.h>
#include <keyboard.h>
#include <serial_debug.h>
#include <timer.h>
#include <mm/pfa.h>
#include <kmalloc.h>
#include <sched.h>
#include <assert.h>
#include <x86.h>
#include <initcalls.h>
#include <drv.h>
#include <mm/slab.h>
#include <fs/vfs.h>
#include <fs/mem.h>
#include <errno.h>
#include <fs/dev.h>
 
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

int init(void *ctx) {
	v_ma *k_arena = ctx;

	/*
		Final setup that may sleep. Interrupts are enabled.
	*/
	struct vfs *root = memfs_new();
	assert(!vfs_mount(root, "/"));
	assert(!vfs_mkdir(vfs_get_root(), "dev", 0666));
	struct vfs *devfs = devfs_get();
	assert(!vfs_mount(devfs, "/dev"));

	serial_enable_buffering();
	driver_init(k_arena);
	keyboard_debug_keystrokes();

	for (;;) {
		tid_t ct = task_create(console_task, NULL, "console_task", 0);
		assert(ct > 0);
		wait_tid(ct);
	}

	vfs_unmount(root);
	assert(NORETURN);
	return -1;
}

void stage1_init(void) {
	terminal_init(VGA_WIDTH, VGA_HEIGHT);
	cli();
	idt_init();
	gdt_init();
	kbd_init();
	serial_setup();
	pfa_init();
	slab_init();
	vma_init();
	run_initcalls();

	uint8_t *k_arena_buf = kmalloc(KERNEL_ARENA_PAGES * PAGE_SIZE);
	v_ma k_arena = v_ma_from_buf(k_arena_buf, KERNEL_ARENA_PAGES * PAGE_SIZE);

	pit_initialize();
	int ret = attach_irq(IRQ0_OFFSET + 0, do_timer, "timer");
	assert(!ret);
	sched_init();

	// FIXME: move to vma.c/h
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
	page_directory[0] = 0;
	flush_cr3();
	uint32_t kernel_bytes = kernel_physical_end - kernel_physical_start;
	kprintf("Kernel image at %h-%h (%ik, %i pages)\n", kernel_physical_start, kernel_physical_end,
	        kernel_bytes / 1024, PAGE_ROUND_UP(kernel_bytes) / PAGE_SIZE);
	// TODO: copy elf header here before freeing the page it's in, once that becomes relevant.
	pf_free((void *)(elf_hdr_with_padding_start + PFA_VIRT_OFFSET));

	vma_spawn_defrag_task();
	task_create(init, &k_arena, "init", 0);
	assert(!(read_eflags() & EFLAGS_IF));

	// Enable interrupts to start scheduling.
	sti();
	for (;;)
		asm("hlt");
}
