#include <x86.h>
#include <panic.h>
#include <utils.h>
#include <sched.h>

volatile int halted = 0;

void cli_push(void) {
	int eflags = read_eflags();
	cli();
	// Store original state of eflags & check before calling
	// sti() in case interrupts were already disabled
	if (!current->cli_depth)
		current->cli_int_enabled = eflags & EFLAGS_IF;
	current->cli_depth++;
}

void cli_pop(void) {
	if (read_eflags() & EFLAGS_IF)
		panic("cli_pop while interruptible");
	if (--current->cli_depth < 0)
		panic("cli_pop undeflow");
	if (!current->cli_depth && current->cli_int_enabled)
		sti();
}

void sti_push(void) {
	int eflags = read_eflags();
	sti();
	// Store original state of eflags & check before calling
	// cli() in case interrupts were already enabled
	if (!current->sti_depth)
		current->sti_int_disabled = !(eflags & EFLAGS_IF);
	current->sti_depth++;
}

void sti_pop(void) {
	if (!(read_eflags() & EFLAGS_IF))
		panic("sti_pop while uninterruptible");
	if (--current->sti_depth < 0)
		panic("sti_pop undeflow");
	if (!current->sti_depth && current->sti_int_disabled)
		cli();
}

#define GDT_ENTRIES 6

struct gdt_entry {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_mid; // bottom 4 bits
	uint8_t access;
	uint8_t flags4_limit4;
	uint8_t base_high;
} __attribute__((packed));

struct gdt_entry gdt_entries[GDT_ENTRIES];

struct gdt_ptr {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed));

static struct descriptor_ptr gdt_ptr = {
	.limit = GDT_ENTRIES * sizeof(gdt_entries[0]) - 1,
	.base = (uint32_t)&gdt_entries[0],
};

void load_gdt(struct descriptor_ptr *p);
asm(
".globl load_gdt\n"
"load_gdt:"
	"mov edx, [esp + 4];"
	"lgdt [edx];"
	"ret;"
);

static void set_gdt_entry(uint32_t idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
	gdt_entries[idx] = (struct gdt_entry){
		.limit_low = limit & 0xFFFF,
		.base_low = base & 0xFFFF,
		.base_mid = base >> 16 & 0xFF,
		.access = access,
		.flags4_limit4 = (flags & 0xF0) | (limit >> 16 & 0x0F),
		.base_high = base >> 24 & 0xFF,
	};
}

struct tss g_tss;

/*
	NOTE: We don't care about segmentation, so this sets up the
	global descriptor table such that all "segments" start
	at 0x00000000 and extend for 0xFFFFF pages (4GB), which
	yields a linear address space where paging can then control
	access to memory.
*/
void gdt_init(void) {
	memset((uint8_t *)&g_tss, 0, sizeof(g_tss));
	g_tss.ss0 = GDT_KERNEL_DATA;
	set_gdt_entry(0, 0, 0, 0, 0);
	/*
		Access byte:
		_____________________________
		| P |  DPL | S | E |DC |RW |A  |
		  |          |   |
		  |          |   \Executable, 0 == data, 1 == code segment
		  |          |
    Present          0 = sys/task seg, 1 = code/data seg

	*/
	//                                                                        Granularity, 0 == byte, 1 == PAGE_SIZE(4096)
	//                                                                        |
	//                                              Type                      |  Size flag, 0 == 16 bit seg, 1 == 32 bit seg
	//                                                 |                      |  |
	//                                        Present  |                      v  v
	//                                              |  |        /- 0b1100 -> |G |DB|L |r |
	//                                              v  v        v
	set_gdt_entry(1, 0, 0x000FFFFF,               0b10011010, 0xC0); // Kernel code segment
	set_gdt_entry(2, 0, 0x000FFFFF,               0b10010010, 0xC0); // Kernel data segment
	set_gdt_entry(3, 0, 0xFFFFFFFF,               0b11111010, 0xC0); // Userland code segment
	set_gdt_entry(4, 0, 0xFFFFFFFF,               0b11110010, 0xC0); // Userland data segment
	set_gdt_entry(5, (uint32_t)&g_tss, sizeof(g_tss), 0b10001001, 0x40); // Task state segment

	load_gdt(&gdt_ptr);
	asm volatile("ltr %[selector];"
		: /* No outputs */
		: [selector]"a"((uint16_t)GDT_TSS)
	);
}
