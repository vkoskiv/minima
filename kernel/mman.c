//
//  mman.c
//
//  Created by Valtteri Koskivuori on 26/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#include "mman.h"
#include "assert.h"
#include <stddef.h>
#include "terminal.h"
#include "panic.h"
#include <vkern.h>
#include "pfa.h"
#include "linker.h"
#include "x86.h"

/* Anatomy of a virtual address
31                                  0
 |  10 bits |  10 bits |   12 bits  |
 |  PD idx  |  PT idx  |   offset   |
 
 */

static struct vma vma_split(struct vma *from, size_t pages);
static void vm_map(struct vma *vm);
static uint32_t *const page_directory = (uint32_t *)0xFFFFF000;

phys_addr get_physical_address(virt_addr virt) {
	uint16_t pd_index = VA_PD_IDX(virt);
	uint16_t pt_index = VA_PT_IDX(virt);
	
	// In stage0.c, we map the last pd entry to the base of the page directory.
	uint32_t *page_directory = (uint32_t *)0xFFFFF000;
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
	size_t size;
	v_ilist linkage;
};

#define MMAN_ARENA_PAGES 8
static uint8_t *mman_buf = NULL;
static v_ma mman_arena;

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

void mman_init(void) {
	assert(!mman_buf);
	virt_addr vma_start = PAGE_ROUND_UP(VIRT_OFFSET + (1 * MB));
	virt_addr vma_end = PAGE_ROUND_DN(PFA_VIRT_OFFSET - PAGE_SIZE);
	struct vma space = {
		.start = vma_start,
		.size = (vma_end - vma_start),
	};
	struct vma arena_vma = vma_split(&space, MMAN_ARENA_PAGES);
	vm_map(&arena_vma);
	mman_buf = (void *)arena_vma.start;
	mman_arena = v_ma_from_buf(mman_buf, MMAN_ARENA_PAGES * PAGE_SIZE);
	v_ma_on_oom(mman_arena) {
		panic("mman arena OOM (%ik)", (MMAN_ARENA_PAGES * PAGE_SIZE) / 1024);
	}

	struct vma *on_arena = v_put(&mman_arena, struct vma, space);

	v_ilist_append(&on_arena->linkage, &vma_freelist);
	kprintf("mman: vma regions:\n");
	dump_vm_range(&vma_freelist, "FREE");
}

static struct vma vma_split(struct vma *from, size_t pages) {
	struct vma new = {
		.start = from->start,
		.size = pages * PAGE_SIZE,
	};
	from->start += (pages * PAGE_SIZE);
	from->size -= (pages * PAGE_SIZE);
	return new;
}

static struct vma *find_space(size_t pages) {
	struct vma *fit = NULL;
	v_ilist *pos;
	size_t bytes = pages * PAGE_SIZE;
	v_ilist_for_each(pos, &vma_freelist) {
		struct vma *area = v_ilist_get(pos, struct vma, linkage);
		// kprintf("a: %i, need %i\n", area->size, bytes);
		if (!fit && area->size >= bytes)
			fit = area;
		if (area->size < fit->size && area->size >= bytes)
			fit = area;
	}
	if (!fit)
		panic("Couldn't find vm space to fit allocation of %i pages", pages);
	if (fit->size > (pages * PAGE_SIZE)) {
		struct vma new = vma_split(fit, pages);
		struct vma *in_arena = v_put(&mman_arena, struct vma, new);
		v_ilist_prepend(&in_arena->linkage, &vma_list);
		return in_arena;
	} else {
		v_ilist_remove(&fit->linkage);
		v_ilist_prepend(&fit->linkage, &vma_list);
		return fit;
	}
}

static void vm_return_to_freelist(struct vma *a) {
	v_ilist_remove(&a->linkage);
	// Will probably have to get smarter about this at some point,
	// but this works for now. find_space() walks the list to find the
	// smallest fit.
	v_ilist_prepend(&a->linkage, &vma_freelist);
}

// TODO: Invent a more generic gadget to deduplicate these virt_addr loops here & in mprotect()
static void vm_map(struct vma *vm) {
	for (virt_addr va = vm->start; va < vm->start + vm->size; va += PAGE_SIZE) {
		// kprintf("%h -> pd[%i] -> pt[%i]\n", va, VA_PD_IDX(va), VA_PT_IDX(va));
		pde_t *pde = &page_directory[VA_PD_IDX(va)];
		if (!((*pde) & PTE_PRESENT))
			*pde = (pde_t)((phys_addr)(pf_alloc() - PFA_VIRT_OFFSET) | PTE_WRITABLE | PTE_PRESENT);
		pte_t *page_table = (pte_t *)(((*pde) & ~0xFFF) + PFA_VIRT_OFFSET);
		pte_t *pte = &page_table[VA_PT_IDX(va)];
		if (!((*pte) & PTE_PRESENT))
			*pte = (pte_t)((phys_addr)(pf_alloc() - PFA_VIRT_OFFSET) | PTE_WRITABLE | PTE_PRESENT);
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
	for (pde_t p = start; p <= last; ++p) {
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
	pages += pages / 1024;
	// Check physical frames first to see if we can satisfy this request
	if (!pf_have_frames(pages + (pages / 1024)))
		return NULL;
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
	v_ilist *pos;
	v_ilist_for_each(pos, &vma_list) {
		struct vma *area = v_ilist_get(pos, struct vma, linkage);
		if (area->start == (virt_addr)ptr) {
			a = area;
			break;
		}
	}
	if (!a)
		panic("Attempted to free unknown vma %h", a);
	vm_unmap(a);
	vm_return_to_freelist(a);
}

void *kmalloc(size_t bytes) {
	if (bytes <= PAGE_SIZE)
		return pf_alloc();
	return vmalloc(bytes);
}

void kfree(void *ptr) {
	if (!ptr)
		return;
	phys_addr addr = (phys_addr)ptr;
	if (addr >= PFA_VIRT_OFFSET)
		pf_free(ptr);
	else {
		vmfree(ptr);
	}
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
	for (virt_addr va = (virt_addr)addr; va < ((virt_addr)addr + len); va += PAGE_SIZE) {
		pde_t *pde = &page_directory[VA_PD_IDX(va)];
		if (!((*pde) & PTE_PRESENT))
			return -1; // mprotect() assumes the memory is already mapped
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
		char present: 1;   // 0 = not present, 1 = protection violation
		char write: 1;     // 0 = read, 1 = write
		char user: 1;      // 0 = supervisor, 1 = user
		char res_write: 1;
		char insn_fetch: 1;
		char prot_key: 1;
		char ss: 1;
		char pad: 8;
		char sgx: 1;
		char reserved: 8;
	};
};

struct pf_regs {
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	union pf_error error;
	uint32_t eip, cs, eflags;
};

void handle_gp_fault(void) {
	panic("GP FAULT");
}

static void panic_with_regs(const char *type, virt_addr addr, struct pf_regs *r) {
	__panic("","",0,
		"%s %s %s %s @ %h\n"
			"\tedi: %h, esi: %h, ebp: %h, esp: %h,\n"
			"\tebx: %h, edx: %h, ecx: %h, eax: %h\n"
			"\terror: %h\n\teip: %h, cs: %h, eflags: %h",
		type, r->error.user ? "user" : "kernel", r->error.present ? "PV" : "NP", r->error.write ? "write" : "read", addr,
		    r->edi, r->esi, r->ebp, r->esp, r->ebx, r->edx, r->ecx, r->eax,
			r->error.value,
			r->eip, r->cs, r->eflags);
}

void handle_page_fault(struct pf_regs *r) {
	virt_addr cr2 = read_cr2();
	panic_with_regs(cr2 ? "PAGE FAULT" : "NULL PAGE", cr2, r);
}
