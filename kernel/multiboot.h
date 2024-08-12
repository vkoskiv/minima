//
//  multiboot.h
//  minima
//
//  Created by Valtteri Koskivuori on 27/01/2021.
//  Copyright © 2021 Valtteri Koskivuori. All rights reserved.
//

#pragma once

#include "stdint.h"

struct multiboot_info *validate_multiboot(uint32_t multiboot_magic, void *multiboot_header);

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

struct multiboot_header {
	uint32_t magic;
	uint32_t flags;
	uint32_t checksum;
	
	// Probably unpopulated
	uint32_t header_address;
	uint32_t load_address;
	uint32_t load_end_address;
	uint32_t bss_end_address;
	uint32_t entry_address;
	
	// Video info
	uint32_t mode_type;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
};

struct multiboot_aout_symbol_table {
	uint32_t table_size;
	uint32_t string_size;
	uint32_t address;
	uint32_t reserved;
};

struct multiboot_elf_section_header_table {
	uint32_t num;
	uint32_t size;
	uint32_t address;
	uint32_t shndx;
};

// The interesting one
struct multiboot_info {
	// Multiboot info version number
	uint32_t flags;
	// Available memory from BIOS
	uint32_t mem_lower;
	uint32_t mem_upper;
	// Root partition
	uint32_t boot_device;
	// Kernel command line
	uint32_t cmdline;
	// Boot module list
	uint32_t mods_count;
	uint32_t mods_addr;
	// Thing
	union {
		struct multiboot_aout_symbol_table aout_sym;
		struct multiboot_elf_section_header_table elf_sec;
	} u;
	// Memory mapping buffer
	uint32_t mmap_length;
	uint32_t mmap_address;
	// Drive info buffer
	uint32_t drives_length;
	uint32_t drives_address;
	// ROM config table
	uint32_t config_table;
	// Bootloader name
	uint32_t boot_loader_name;
	// APM table
	uint32_t apm_table;
	// Video stuff
	uint32_t vbe_control_info;
	uint32_t vbe_mode_info;
	uint16_t vbe_mode;
	uint16_t vbe_interface_seg;
	uint16_t vbe_interface_off;
	uint16_t vbe_interface_length;
	
	uint64_t framebuffer_address;
	uint32_t framebuffer_pitch;
	uint32_t framebuffer_width;
	uint32_t framebuffer_height;
	uint8_t  framebuffer_bits_per_pixel;
#define MB_FRAMEBUFFER_TYPE_INDEXED 0
#define MB_FRAMEBUFFER_TYPE_RGB 1
#define MB_FRAMEBUFFER_TYPE_EGA_TEXT 2
	uint8_t framebuffer_type;
	union {
		struct {
			uint32_t framebuffer_palette_address;
			uint16_t framebuffer_palette_num_colors;
		};
		struct {
			uint8_t framebuffer_red_field_position;
			uint8_t framebuffer_red_mask_size;
			uint8_t framebuffer_green_field_position;
			uint8_t framebuffer_green_mask_size;
			uint8_t framebuffer_blue_field_position;
			uint8_t framebuffer_blue_mask_size;
		};
	};
};

struct multiboot_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

struct multiboot_mmap_entry {
	uint32_t size;
#if 0
	uint64_t address;
	uint64_t length;
#else
	uint32_t addr_lo;
	uint32_t addr_hi;
	uint32_t length_lo;
	uint32_t length_hi;
#endif
#define MB_MEMORY_AVAILABLE 1
#define MB_MEMORY_RESERVED 2
#define MB_MEMORY_ACPI_RECLAIMABLE 3
#define MB_MEMORY_NVS 4
#define MB_MEMORY_BADRAM 5
	uint32_t type;
} __attribute__((packed));

struct multiboot_mod_list {
	// Memory goes from mod_start -> mod_end - 1 inclusive
	uint32_t mod_start;
	uint32_t mod_end;
	// Module cmdline
	uint32_t cmdline;
	// Padding
	uint32_t pad;
};

// APM BIOS INFO
struct multiboot_apm_info {
	uint16_t version;
	uint16_t cseg;
	uint16_t offset;
	uint16_t cseg_16;
	uint16_t dseg;
	uint16_t flags;
	uint16_t cseg_len;
	uint16_t cseg_16_len;
	uint16_t dseg_len;
};
