//
//  vma.c - virtual memory allocator
//

#include <stddef.h>
#include <vkern.h>
#include <mm/vma.h>
#include <assert.h>
#include <kprintf.h>
#include <panic.h>
#include <mm/pfa.h>
#include <linker.h>
#include <x86.h>
#include <sched.h>
#include <mm/slab.h>

/* Anatomy of a virtual address
31                                  0
 |  10 bits |  10 bits |   12 bits  |
 |  PD idx  |  PT idx  |   offset   |
 
 */

static uint32_t *const page_directory = (uint32_t *)0xFFFFF000;

phys_addr get_physical_address(virt_addr virt) {
	uint16_t pd_index = VA_PD_IDX(virt);
	uint16_t pt_index = VA_PT_IDX(virt);
	
	// In stage0.c, we map the last pd entry to the base of the page directory.
	if (!(page_directory[1023] & PTE_PRESENT))
		panic("Last page directory entry not present");
	uint32_t pde = page_directory[pd_index];
	if (!(pde & PTE_PRESENT))
		panic("PD entry %i (%h) not present", (uint32_t)pd_index, pde);

	uint32_t *page_table = (uint32_t *)((pde & ~0xFFF) + PFA_VIRT_OFFSET);

	uint32_t pte = page_table[pt_index];
	if (!(pte & PTE_PRESENT))
		panic("PT entry %i (%h) not present", (uint32_t)pt_index, pte);

	return (pte & ~0xFFF) + VA_PG_OFF(virt);
}

static V_ILIST(vma_list);
static V_ILIST(vma_freelist);

struct vma {
	virt_addr start;
	// TODO: consider replacing size with num of pages, since these are page size granular anyway
	size_t size;
	v_ilist linkage;
};

static void dump_vm_range(v_ilist *list, const char *type) {
	v_ilist *pos;
	v_ilist_for_each(pos, list) {
		struct vma *a = v_ilist_get(pos, struct vma, linkage);
		kprintf("\t%s: %h-%h (%ipg, %ik)\n", type, a->start, a->start + a->size, a->size / PAGE_SIZE, a->size / KB);
	}
}

void dump_vm_ranges(const char *txt) {
	kprintf("%s:\n", txt);
	dump_vm_range(&vma_freelist, "FREE");
	dump_vm_range(&vma_list, "USED");
	kprintf("\tTotal %i free, %i in use\n", v_ilist_count(&vma_freelist), v_ilist_count(&vma_list));
}

static V_ILIST(defrag_queue);
static SEMAPHORE(defrag_call, 0);
// Only do defrag every this many vmfree() calls.
#define VMA_DEFRAG_INTERVAL 32
static int vma_merge(struct vma *a, struct vma *into);

static int vma_cmp(v_ilist *l, v_ilist *r) {
	struct vma *lhs = v_ilist_get(l, struct vma, linkage);
	struct vma *rhs = v_ilist_get(r, struct vma, linkage);
	int ret;
	if ((lhs->start + lhs->size) <= rhs->start)
		ret = -1;
	if ((lhs->start + lhs->size) == rhs->start)
		ret = 0;
	if (lhs->start >= (rhs->start + rhs->size))
		ret = 1;
	return ret;
}

static void do_defrag(void) {
	cli_push();
	v_ilist *pos, *temp;
	v_ilist_for_each_safe(pos, temp, &defrag_queue) {
		struct vma *a = v_ilist_get(pos, struct vma, linkage);
		v_ilist_remove(&a->linkage);
		v_ilist_insert(&a->linkage, &vma_freelist, vma_cmp);
	}

	v_ilist_for_each_safe(pos, temp, &vma_freelist) {
		struct vma *a = v_ilist_get(pos, struct vma, linkage);
		struct vma *lhs = v_ilist_get(a->linkage.prev, struct vma, linkage);
		if (!v_ilist_is_head(&vma_freelist, a->linkage.prev) && vma_merge(a, lhs)) {
			v_ilist_remove(&a->linkage);
			slab_free(a);
			continue;
		}
		struct vma *rhs = v_ilist_get(a->linkage.next, struct vma, linkage);
		if (!v_ilist_is_head(&vma_freelist, a->linkage.next) && vma_merge(a, rhs)) {
			v_ilist_remove(&a->linkage);
			slab_free(a);
			continue;
		}
	}
	cli_pop();
}

static int vma_defrag(void *ctx) {
	(void)ctx;
	for (;;) {
		for (int i = 0; i < VMA_DEFRAG_INTERVAL; ++i)
			sem_pend(&defrag_call);
		do_defrag();
	}
	return 0;
}

void vma_spawn_defrag_task(void) {
	task_create(vma_defrag, NULL, "kvmdefrag", 0);
}

void vma_init(void) {
	virt_addr vma_start = PAGE_ROUND_UP(VIRT_OFFSET + (1 * MB));
	virt_addr vma_end = PAGE_ROUND_DN(PFA_VIRT_OFFSET - PAGE_SIZE);
	struct vma *initial = slab_alloc(sizeof(*initial));
	assert(initial);
	*initial = (struct vma){
		.start = vma_start,
		.size = (vma_end - vma_start),
	};
	v_ilist_append(&initial->linkage, &vma_freelist);
	kprintf("mm/vma: kernel vm regions:\n");
	dump_vm_range(&vma_freelist, "FREE");
}

static struct vma *vma_split(struct vma *from, size_t pages) {
	struct vma *new = slab_alloc(sizeof(*new));
	*new = (struct vma){
		.start = from->start,
		.size = pages * PAGE_SIZE,
	};
	v_ilist_init(&new->linkage);
	from->start += (pages * PAGE_SIZE);
	from->size -= (pages * PAGE_SIZE);
	return new;
}

static void assert_invariants(struct vma *a, struct vma *b) {
	assert(a->size);
	assert(b->size);
	assert(a->size >= PAGE_SIZE);
	assert(b->size >= PAGE_SIZE);
	uintptr_t a_s = a->start;
	uintptr_t a_e = a->start + a->size;
	uintptr_t b_s = b->start;
	uintptr_t b_e = b->start + b->size;
	// Check they don't overlap
	assert(!(a_s >= b_s && a_s < b_e));
	assert(!(a_e > b_s && a_e < b_e));
}

static int vma_merge(struct vma *a, struct vma *into) {
	assert_invariants(a, into);
	// Two cases:
	// a->end == into->start, so merge to start of into
	// a->beg == into->end, then merge to end of into

	if ((a->start + a->size) == into->start) {
		into->start = a->start;
		into->size += a->size;
		a->size = 0;
		return 1;
	}
	if (a->start == (into->start + into->size)) {
		into->size += a->size;
		a->size = 0;
		a->start = into->start + into->size;
		return 1;
	}
	return 0;
}

static struct vma *find_space(size_t pages) {
	struct vma *fit = NULL;
	v_ilist *pos;
	size_t bytes = pages * PAGE_SIZE;
	v_ilist_for_each(pos, &vma_freelist) {
		struct vma *area = v_ilist_get(pos, struct vma, linkage);
		if (!fit && area->size >= bytes)
			fit = area;
		if (fit && area->size < fit->size && area->size >= bytes)
			fit = area;
	}
	if (!fit)
		panic("Couldn't find vm space to fit allocation of %i pages", pages);
	if (fit->size > (pages * PAGE_SIZE)) {
		struct vma *new = vma_split(fit, pages);
		v_ilist_prepend(&new->linkage, &vma_list);
		return new;
	} else {
		v_ilist_remove(&fit->linkage);
		v_ilist_prepend(&fit->linkage, &vma_list);
		return fit;
	}
}

// FIXME: demand paging. Just mark as present here, and populate in do_page_fault.
// TODO: Invent a more generic gadget to deduplicate these virt_addr loops here & in mprotect()
static void vm_map(struct vma *vm) {
	for (virt_addr va = vm->start; va < vm->start + vm->size; va += PAGE_SIZE) {
		// kprintf("%h -> pd[%i] -> pt[%i]\n", va, VA_PD_IDX(va), VA_PT_IDX(va));
		pde_t *pde = &page_directory[VA_PD_IDX(va)];
		if (!((*pde) & PTE_PRESENT))
			*pde = (pde_t)((phys_addr)(pf_zalloc() - PFA_VIRT_OFFSET) | PTE_WRITABLE | PTE_PRESENT);
		pte_t *page_table = (pte_t *)(((*pde) & ~0xFFF) + PFA_VIRT_OFFSET);
		pte_t *pte = &page_table[VA_PT_IDX(va)];
		if (!((*pte) & PTE_PRESENT))
			*pte = (pte_t)((phys_addr)(pf_zalloc() - PFA_VIRT_OFFSET) | PTE_WRITABLE | PTE_PRESENT);
	}
	flush_cr3();
}

static void vm_unmap(struct vma *vm) {
	for (virt_addr va = vm->start; va < vm->start + vm->size; va += PAGE_SIZE) {
		uint32_t pd_idx = VA_PD_IDX(va);
		pde_t pde = page_directory[pd_idx];
		assert(pde & PTE_PRESENT);
		pte_t *page_table = (pte_t *)((pde & ~0xFFF) + PFA_VIRT_OFFSET);
		pte_t *pte = &page_table[VA_PT_IDX(va)];
		assert((*pte) & PTE_PRESENT);
		pf_free((void *)(((*pte) & ~0xFFF) + PFA_VIRT_OFFSET));
		*pte = 0;
	}
	// Also sweep empty page tables
	pde_t start = VA_PD_IDX(vm->start);
	pde_t last = VA_PD_IDX(vm->start + vm->size);
	for (pde_t p = start; p < last; ++p) {
		int empty = 1;
		pte_t *table = (pte_t *)((page_directory[p] & ~0xFFF) + PFA_VIRT_OFFSET);
		for (size_t i = 0; i < 1024; ++i) {
			if (table[i]) {
				empty = 0;
				break;
			}
		}
		if (!empty)
			continue;
		page_directory[p] = PTE_WRITABLE;
		pf_free(table);
	}
	flush_cr3();
}

void *vmalloc(size_t bytes) {
	size_t pages = PAGE_ROUND_UP(bytes) / PAGE_SIZE;
	cli_push();
	struct vma *vma = find_space(pages);
	cli_pop();
	if (!vma)
		panic("Out of VM address space allocating %i bytes", bytes);
	vm_map(vma);
	return (void *)vma->start;
}

void vmfree(void *ptr) {
	struct vma *a = NULL;
	// FIXME: linear scan for every free. Maybe look into something like an interval tree?
	v_ilist *pos;
	v_ilist_for_each(pos, &vma_list) {
		struct vma *area = v_ilist_get(pos, struct vma, linkage);
		if (area->start == (virt_addr)ptr) {
			a = area;
			break;
		}
	}
	if (!a)
		panic("Attempted to free unknown vma %h", ptr);
	vm_unmap(a);
	v_ilist_remove(&a->linkage); // remove from vma_list
	v_ilist_prepend(&a->linkage, &defrag_queue);
	sem_post(&defrag_call);
}

/*
	NOTE:
	I didn't look how PROT flags actually map to page table flags, so this is my best guess:
	- PROT_READ -> PTE_PRESENT (P)
	- PROT_WRITE -> PTE_WRITABLE (R/W)
*/
int mprotect(void *addr, size_t len, int prot) {
	uint32_t flags = 0;
	if (prot & PROT_READ)
		flags |= PTE_PRESENT;
	if (prot & PROT_WRITE)
		flags |= PTE_WRITABLE;
	if (prot & PROT_USR)
		flags |= PTE_USER;
	for (virt_addr va = (virt_addr)addr; va < ((virt_addr)addr + len); va += PAGE_SIZE) {
		pde_t *pde = &page_directory[VA_PD_IDX(va)];
		if (!((*pde) & PTE_PRESENT))
			return -1; // mprotect() assumes the memory is already mapped
		*pde = *pde & ~0xFFF;
		*pde |= flags;
		if (!*pde)
			return -1;
		pte_t *page_table = (pte_t *)(((*pde) & ~0xFFF) + PFA_VIRT_OFFSET);
		pte_t *pte = &page_table[VA_PT_IDX(va)];
		if (!((*pte) & PTE_PRESENT) && (prot & PROT_WRITE))
			return -1; // Trying to mark a non-present page writable
		*pte = *pte & ~0xFFF; // Clear old flags
		*pte |= flags; // Set new flags
		if (!*pte)
			return -1;
	}
	flush_cr3();
	return 0;
}

/*
	bit 0 (P): 0 = non-present page, 1 = protection violation
	bit 1 (W/R): 0 = read, 1 = write
	bit 2 (U/S): 0 = supervisor, 1 = user
	bit 3 (RSVD): reserved bit violation
	bit 4 (I/D): instruction fetch (if supported)
*/
union pf_error {
	uint32_t value;
	struct {
		unsigned char present: 1;   // 0 = not present, 1 = protection violation
		unsigned char write: 1;     // 0 = read, 1 = write
		unsigned char user: 1;      // 0 = supervisor, 1 = user
		unsigned char res_write: 1;
		unsigned char insn_fetch: 1;
		unsigned char prot_key: 1;
		unsigned char ss: 1;
		unsigned char pad: 8;
		unsigned char sgx: 1;
		unsigned char reserved: 8;
	};
};

void dumpregs(virt_addr addr, const struct irq_regs *const regs) {
	union pf_error error;
	error.value = regs->error;
	kprintf("%s %s %s @ %h\n"
			"\tedi: %h, esi: %h, ebp: %h, esp: %h,\n"
			"\tebx: %h, edx: %h, ecx: %h, eax: %h\n"
			"\terror: %h\n\teip: %h, cs: %h, eflags: %h\n",
			error.user ? "user" : "kernel", error.present ? "PV" : "NP", error.write ? "write" : "read", addr,
		    regs->edi, regs->esi, regs->ebp, regs->esp, regs->ebx, regs->edx, regs->ecx, regs->eax,
			error.value,
			regs->eip, regs->cs, regs->eflags);
}

static void panic_with_regs(const char *type, virt_addr addr, const struct irq_regs *const regs) {
	union pf_error error;
	error.value = regs->error;
	__panic("","",0,
		"%s %s %s %s @ %h\n"
			"\tedi: %h, esi: %h, ebp: %h, esp: %h,\n"
			"\tebx: %h, edx: %h, ecx: %h, eax: %h\n"
			"\terror: %h\n\teip: %h, cs: %h, eflags: %h",
		type, error.user ? "user" : "kernel", error.present ? "PV" : "NP", error.write ? "write" : "read", addr,
		    regs->edi, regs->esi, regs->ebp, regs->esp, regs->ebx, regs->edx, regs->ecx, regs->eax,
			error.value,
			regs->eip, regs->cs, regs->eflags);
}

/*
	Stack layout after function prologue on i386 (assumes -fno-omit-frame-pointer):
	...
	[ebp + 12]: arg2
	[ebp +  8]: arg1
	[ebp +  4]: return addr
	[ebp +  0]: current frame pointer (mov ebp, esp)
*/
struct stack_frame {
	struct stack_frame *prev;
	uintptr_t return_address;
};

static const uint32_t s_bt_maxdepth = 128;

/*
	FIXME: The eip values printed here are actually for the next instruction
	after the call. To correct for that, I think I could try to decode the
	call instruction and fix the offset based on the type.
	This page: https://www.felixcloutier.com/x86/call
	says that the variants are:
	0xE8 (call rel16) (3 bytes? didn't see any of these with 'make od')
	0xE8 (call rel32) (5 bytes, seems like 99% of calls in the kernel are this type)
	0xFF (call r/m16, r/m32), 2 bytes, so 0xff and reg. I guess uses modr/m then? I saw
	a bunch of these for e.g. switch-case in do_syscall to 'call eax'
		FF D0 = call eax
		FF D2 = call edx
		   ^ Pretty sure that's modr/m?

	Some others too, but didn't see any other than E8 and FF so far. So should be pretty
	doable to decode these and fix up the line number, but I'm fine with this as is for
	the time being. Much better than no backtrace!
*/

static int valid_frame(struct stack_frame *f) {
	if (!f)
		return 0;
	uintptr_t addr = (uintptr_t)f;
	void *stack_bottom = current->stack_user;
	if (!stack_bottom)
		stack_bottom = current->stack_kernel;
	assert(stack_bottom);
	uintptr_t sb = (uintptr_t)stack_bottom;
	if (addr < sb)
		return 0; // redzone overflow
	if (addr < (sb + (TASK_STACK_PAGES * PAGE_SIZE)))
		return 0; // stack overflow (into redzone)
	if (addr > (sb + (2 * (TASK_STACK_PAGES * PAGE_SIZE))))
		return 0; // stack underflow
	return 1;
}

static void dump_backtrace(uint32_t ebp, uint32_t eip) {
	struct stack_frame *f = (struct stack_frame *)ebp;
	uint32_t depth = 0;
	kprintf("Backtrace:\n");
	kprintf("\t[%u] ebp:%h, eip:%h\n", depth, f, eip);
	while (valid_frame(f) && depth < s_bt_maxdepth) {
		kprintf("\t[%u] ebp:%h, eip:%h\n", depth + 1, f, f->return_address);
		f = f->prev;
		depth++;
	}
}

static void dump_gpfault_reason(uint32_t e) {
	uint8_t type = (e >> 1) & 0x03;
	uint16_t idx = (e & 0xFFFF) >> 3;
	switch (type) {
	case 0: kprintf("BAD GDT idx %i\n", idx);
		break;
	case 1: kprintf("BAD IDT idx %i\n", idx);
		break;
	case 2: kprintf("BAD LDT idx %i\n", idx);
		break;
	case 3: kprintf("BAD IDT idx %i\n", idx);
		break;
	}
}

void do_gp_fault(const struct irq_regs *const regs) {
	dump_backtrace(regs->ebp, (uint32_t)regs->eip);
	virt_addr cr2 = read_cr2();
	if (current && current->stack_user) {
		kprintf("GP fault, killing %s[%i]\n", current->name, current->id);
		dumpregs(cr2, regs);
		current->state = ts_stopping;
		sched();
	}
	if (regs->error)
		dump_gpfault_reason(regs->error);
	panic_with_regs("GP FAULT", cr2, regs);
}

void do_page_fault(const struct irq_regs *const regs) {
	dump_backtrace(regs->ebp, (uint32_t)regs->eip);
	virt_addr cr2 = read_cr2();
	if (current) {
		kprintf("page fault, killing %s[%i]\n", current->name, current->id);
		dumpregs(cr2, regs);
		current->state = ts_stopping;
		sched();
	}
	panic_with_regs(cr2 ? "PAGE FAULT" : "NULL PAGE", cr2, regs);
}
