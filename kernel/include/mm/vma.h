//
//  vma.h - virtual memory allocator API
//

#ifndef _VMA_H_
#define _VMA_H_

#include <stddef.h>
#include <stdint.h>
#include <linker.h>
#include <idt.h>
#include <mm/types.h>

#define IS_PAGE_ALIGNED(addr) (!((addr) & 0x00000fff))

#if !IS_PAGE_ALIGNED(STACK_TOP)
#error "STACK_TOP not page aligned"
#endif
#if !IS_PAGE_ALIGNED(STACK_SIZE)
#error "STACK_SIZE not page aligned"
#endif
#if !IS_PAGE_ALIGNED(KERNEL_PHYS_ADDR)
#error "KERNEL_PHYS_ADDR not page aligned"
#endif
#if !IS_PAGE_ALIGNED(VIRT_OFFSET)
#error "VIRT_OFFSET not page aligned"
#endif
#if !IS_PAGE_ALIGNED(PFA_VIRT_OFFSET)
#error "PFA_VIRT_OFFSET not page aligned"
#endif

#define BOOTLOADER_PHYS_ADDR 0x7c00
#define BOOTLOADER_SIZE      512

#if KERNEL_PHYS_ADDR <= (BOOTLOADER_PHYS_ADDR + BOOTLOADER_SIZE)
#error "Kernel image overlaps with bootloader at 0x7C00-0x7E00"
#endif

#define STACK_BOTTOM (STACK_TOP - STACK_SIZE)

#define PAGE_ROUND_UP(x) ((((uint32_t)(x)) + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1)))
#define PAGE_ROUND_DN(x) (((uint32_t)(x)) & (~(PAGE_SIZE - 1)))

#define P2V(addr) ((void *)(((char *)(addr)) + VIRT_OFFSET))
#define V2P(addr) (((uint32_t)(addr)) - VIRT_OFFSET)

#define VA_PD_IDX(va) ((virt_addr)((va) & 0xFFC00000) >> 22)
#define VA_PT_IDX(va) ((virt_addr)((va) & 0x003FF000) >> 12)
#define VA_PG_OFF(va) ((virt_addr)((va) & 0x00000FFF))

#define PTE_PRESENT           (0x1 << 0)
// NOTE: PTE_WRITABLE only applies to CPL 3. In kernel mode
// clearing this flag does nothing (Intel 80386 Ref. 6.4.1.1)
#define PTE_WRITABLE          (0x1 << 1)
#define PTE_USER              (0x1 << 2)
#define PTE_DISABLE_WT        (0x1 << 3)
#define PTE_DISABLE_CACHE     (0x1 << 4)
#define PTE_ACCESSED          (0x1 << 5)

// FIXME: I'm not sure why these packed unions don't work as I expect.
// Maybe get rid of them if I can't get them working.
// Or just get rid of them anyway.

// typedef union {
// 	uint32_t addr;
// 	struct {
// 		uint16_t pd_idx : 10;
// 		uint16_t pt_idx : 10;
// 		uint16_t offset : 12;
// 	} __attribute((packed)) idx;
// } __attribute((packed)) virt_addr;

// typedef struct {
// 	phys_addr pte_addr: 20;
// 	unsigned char avl_2: 4;
// 	unsigned char is_4mb: 1;
// 	unsigned char available: 1;
// 	unsigned char accessed: 1;
// 	unsigned char cache_disabled: 1;
// 	unsigned char wt_disabled: 1;
// 	unsigned char user: 1;
// 	unsigned char writable: 1;
// 	unsigned char present: 1;
// } pde_t;

typedef uint32_t pde_t;
struct page_directory {
	pde_t entries[1024];
};

// typedef struct {
// 	phys_addr page_addr: 20;
// 	unsigned char avl: 3;
// 	unsigned char global: 1;
// 	unsigned char pat: 1;
// 	unsigned char dirty: 1;
// 	unsigned char accessed: 1;
// 	unsigned char cache_disabled: 1;
// 	unsigned char wt_disabled: 1;
// 	unsigned char user: 1;
// 	unsigned char writable: 1;
// 	unsigned char present: 1;
// } pte_t;

typedef uint32_t pte_t;
struct page_table {
	pte_t entries[1024];
};

// TODO: Consider renaming to flush_tlb?
static inline void flush_cr3(void) {
	asm volatile(
		"mov eax, cr3;"
		"mov cr3, eax;"
		: /* No outputs */
		: /* No inputs */
		: "eax"
	);
}

void vma_init(void);

void dump_vm_ranges(const char *txt);
void *vmalloc(size_t bytes);
void vmfree(void *ptr);

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_NONE  0x0
// FIXME: Remove vvvvvvv
#define PROT_USR   0x3
int mprotect(void *addr, size_t len, int prot);

phys_addr get_physical_address(virt_addr virt);

void do_gp_fault(const struct irq_regs *const regs);
void do_page_fault(const struct irq_regs *const regs);

void dumpregs(virt_addr addr, const struct irq_regs *const regs);

#endif
