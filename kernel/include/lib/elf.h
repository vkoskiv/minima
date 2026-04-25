#ifndef _ELF_H_
#define _ELF_H_

#include <types.h>

typedef u32 eptr32;
typedef u64 eptr64;
typedef u32 e_off32;
typedef u64 e_off64;
typedef u32 vaddr32;
typedef u64 vaddr64;
typedef u32 paddr32;
typedef u64 paddr64;

enum elf_type {
	et_none = 0,
	et_relocatable,
	et_executable,
	et_shared_object,
	et_core,
};

#define ELF_CLASS32 1
#define ELF_CLASS64 2

struct elf_ident {
	u32 magic;
	u8 class;
	u8 byteorder; // startianness, 1 == little, 2 == big?
	u8 version;
	u8 abi;
	u8 abiversion;
	u8 pad[7];
};

struct elf_file32 {
	struct elf_ident ident;
	// Fields below are interpreted according to ident.byteorder
	u16 type_elf; // elf_type
	u16 type_machine;
	u32 version; // again? ident already has version
	eptr32 entry;
	e_off32 program_header_table;
	e_off32 section_header_table;
	u32 flags;
	u16 header_size;
	u16 program_header_entry_size;
	u16 n_program_headers;
	u16 section_header_entry_size;
	u16 n_section_headers;
	u16 section_name_index; // index into section header table
};

struct elf_file64 {
	struct elf_ident ident;
	// Fields below are interpreted according to ident.byteorder
	u16 type_elf; // elf_type
	u16 type_machine;
	u32 version; // again? ident already has version
	eptr64 entry;
	e_off64 program_header_table;
	e_off64 section_header_table;
	u32 flags;
	u16 header_size;
	u16 program_header_entry_size;
	u16 n_program_headers;
	u16 section_header_entry_size;
	u16 n_section_headers;
	u16 section_name_index; // index into section header table
};

enum segment_type {
	st_null = 0,
	st_load,
	st_dyn_link_info,
	st_interpreter,
	st_note,
	st_shlib,
	st_prog_header,
	st_tls_template,
	st_os_range_start   = 0x60000000,
	st_os_range_end     = 0x6fffffff,
	st_proc_range_start = 0x70000000,
	st_proc_range_end   = 0x7fffffff,
};

struct prog_hdr32 {
	enum segment_type type : 32; // FIXME: check this works as expected
	e_off32 offset;
	vaddr32 virt_addr;
	paddr32  phys_addr;
	// TODO: What does seg_bytes != mem_bytes mean?
	u32 seg_bytes;
	u32 mem_bytes;
	u32 flags;
	u32 alignment;
};

struct prog_hdr64 {
	enum segment_type type : 32; // FIXME: check this works as expected
	u32 flags;
	e_off64 offset;
	vaddr64 virt_addr;
	paddr64 phys_addr;
	u64 seg_bytes;
	u64 mem_bytes;
	u64 alignment;
};

enum section_header_type {
	sht_null = 0,
	sht_progbits,
	sht_symtab,
	sht_strtab,
	sht_rela,
	sht_hash,
	sht_dynamic,
	sht_note,
	sht_nobits,
	sht_rel,
	sht_shlib,
	sht_dynsym,
	sht_ctor_array = 0xE, // init
	sht_dtor_array, // fini
	sht_preinit_array,
	sht_group,
	sht_symtab_shndx,
	sht_num,
};

struct section_hdr32 {
	u32 name; // .shstrtab section offset
	enum section_header_type type : 32;
	u32 flags;
	vaddr32 address;
	e_off32 offset;
	u32 bytes;
	u32 link_idx;
	u32 info;
	u32 alignment;
	u32 entry_size;
};

struct section_hdr64 {
	u32 name; // .shstrtab section offset
	enum section_header_type type : 32;
	u64 flags;
	vaddr64 address;
	e_off64 offset;
	u64 bytes;
	u32 link_idx;
	u32 info;
	u64 alignment;
	u64 entry_size;
};

struct elf32 {
	const struct elf_file32 *file;
	const struct prog_hdr32 *phdrs;
	const struct section_hdr32 *shdrs;
	size_t n_phdrs;
	size_t n_shdrs;
};

#endif
