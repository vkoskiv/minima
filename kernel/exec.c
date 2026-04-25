#include <errno.h>
#include <stdint.h>
#include <kprintf.h>
#include <types.h>
#include <utils.h>
#include <fs/vfs.h>
#include <kmalloc.h>
#include <mm/vma.h>
#include <sched.h>
#include <lib/elf.h>

static const uint8_t elf_hdr[] = { 0x7F, 'E', 'L', 'F' };

static int load_segment(struct vfs_file *file, struct prog_hdr32 *ph, struct task *task) {
	void *ptr = mmap(task, ph->virt_addr, ph->mem_bytes, PROT_READ|PROT_WRITE|PROT_USR, 0, file, ph->offset);
	if (!ptr)
		return -ENOMEM;
	return 0;
}

static int load_32(struct vfs_file *file, struct task *task, void **entry_out) {
	struct elf_file32 elf;
	int ret = vfs_read_at(file, &elf, sizeof(elf), 0);
	if (ret < 0)
		return ret;
	if (entry_out)
		*entry_out = (void *)elf.entry;
	if (sizeof(struct prog_hdr32) != elf.program_header_entry_size) {
		kprintf("sizeof(struct prog_hdr32) != elf.program_header_entry_size");
		return 1;
	}
	if (sizeof(struct section_hdr32) != elf.section_header_entry_size) {
		kprintf("sizeof(struct section_hdr32) != elf.section_header_entry_size");
		return 1;
	}
	size_t n_phdrs = elf.n_program_headers;

	struct prog_hdr32 *phdrs = kmalloc(elf.n_program_headers * sizeof(*phdrs));
	ret = vfs_read_at(file, phdrs, elf.n_program_headers * sizeof(*phdrs), elf.program_header_table);
	if (ret < 0) {
		kfree(phdrs);
		return ret;
	}

	for (size_t i = 0; i < n_phdrs; ++i) {
		struct prog_hdr32 *ph = &phdrs[i];
		if (ph->type != st_load) {
			kprintf("Skip unsupported '%s' %h-%h -> %h-%h\n",
			    ph->offset, ph->offset + ph->seg_bytes,
				ph->virt_addr, ph->virt_addr + ph->seg_bytes);
			 continue;
		}
		if ((ret = load_segment(file, ph, task)))
			return ret;
	}
	kfree(phdrs);

	return ret;
}

int elf_load(struct vfs_file *file, struct task *task, void **entry_out) {
	struct elf_ident ident;
	int ret = vfs_read_at(file, &ident, sizeof(ident), 0);
	if (ret < 0)
		return ret;
	if (memcmp(&ident.magic, elf_hdr, sizeof(elf_hdr))) {
		kprintf("Unexpected magic\n");
		return 1;
	}
	switch (ident.class) {
	case ELF_CLASS32:
		return load_32(file, task, entry_out);
	case ELF_CLASS64:
	default:
		kprintf("Unsupported ELF class %u\n", ident.class);
		return -ENOEXEC;
	}

	return 0;
}

int exec(const char *cmd) {
	struct vfs_file *bin = vfs_open_file(cmd);
	if (!bin)
		return -ENOENT;
	struct task *task = NULL;
	tid_t tid = task_prepare(NULL, NULL, cmd, 1, &task);
	if (tid < 0)
		return tid;
	int (*entry)(void *) = NULL;
	int ret = elf_load(bin, task, (void **)&entry);
	if (ret < 0) {
		kprintf("exec: %s\n", strerror(ret));
		return ret;
	}
	task_update_entry(task, entry);
	// TODO: args/env?
	to_runqueue(task);
	ret = wait_tid(tid);
	vfs_close(bin);

	task_unmap(task);
	return ret;
}
