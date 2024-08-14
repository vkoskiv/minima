extern void kernel_main();
extern void _start() {
	kernel_main();
	asm volatile("cli; hlt");
}
