# Constants shared by bootloader, kernel code & linker script.
SHARED_DEFINES=\
	-DVERSION=\"0.0.10\" \
	-DELF_HDR_SIZE=0x1000 \
	-DSTACK_TOP=0x4000 \
	-DSTACK_SIZE=0x4000 \
	-DKERNEL_PHYS_ADDR=0x8000 \
	-DVIRT_OFFSET=0xC0000000 \
	-DPFA_VIRT_OFFSET=0xD0000000 \

TOOLCHAIN=../toolchain/local/bin
TRIPLE=i686-pc-minima-
CC=$(TOOLCHAIN)/$(TRIPLE)gcc
LD=$(TOOLCHAIN)/$(TRIPLE)ld
OBJCOPY=$(TOOLCHAIN)/$(TRIPLE)objcopy
STRIP=$(TOOLCHAIN)/$(TRIPLE)strip
AS=nasm
OPT_LEVEL=-O0
CFLAGS=$(SHARED_DEFINES) -masm=intel -Iinclude/ -ggdb3 -std=gnu99 -ffreestanding -march=i486 $(OPT_LEVEL) -Wall -Wextra -nostdlib -lgcc
ASFLAGS=$(SHARED_DEFINES)
QEMU=qemu-system-i386
QEMUFLAGS=-no-reboot -display gtk,show-menubar=off -m 32 -fda $<

all: qemu

qemu2drv: 1440k.img 720k.img
	$(QEMU) $(QEMUFLAGS) -fdb 1440k_copy.img

qemu%: %k.img
	$(QEMU) $(QEMUFLAGS) -debugcon stdio

qemu: qemu1440

# "hot" reload. To rebuild & restart qemu whenever you save a file, do:
# make reload T=qemu
# If you want to do the same while comparing qemu and bochs:
# make reload T='qemu bochs'
# If you just want to check the kernel builds:
# make reload T=kernel.bin
reload:
	find . -name Makefile -or -name '*.S' -or -name '*.c' -or -name '*.h' | entr -r sh -c 'make $(T)'

debug: 1440k.img
	$(QEMU) $(QEMUFLAGS) -d int,pcall,cpu_reset,guest_errors,invalid_mem,mmu -debugcon stdio

gdb%: %k.img
	gdb kernel.bin -ex "b halt" -ex "target remote | exec $(QEMU) $(QEMUFLAGS) -gdb stdio"

gdb: gdb1440

# NOTE: Use bochs to debug bootloader. make bochs, ^C, lb 0x7c00, then s to step.
bochs: 1440k.img kernel.sym
	BXSHARE=/home/vkoskiv/git/Bochs/bochs/bios/ /home/vkoskiv/git/Bochs/bochs/bochs -f .bochsrc -q -rc .bochs_cmds

bochsi:
	ls Makefile *.S *.c *.h | entr -r sh -c 'make bochs'

# These files are dependent on SHARED_DEFINES in this makefile
USES_SHARED_DEFINES=\
	mbr720.bin \
	mbr1440.bin \
	_linker.ld \
	mman.o \

$(USES_SHARED_DEFINES): Makefile

%.o: %.c
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

# new NASM files
%.o: %.S
	$(AS) $(ASFLAGS) -f elf32 $<

wrflop%: %k.img
	scripts/flop_write.sh $<

wrflop: wrflop1440

# The bootloader has both 16 and 32-bit sections. I don't know of a way to
# disasemble both correctly at the same time, so these both only show valid
# disassembly for the corresponding sections marked in mbr.S
dumpmbr16: mbr1440.bin
	objdump --disassembler-color=on -M intel -D -b binary -mi386 -Maddr16,data16 $< | less -R

dumpmbr32: mbr1440.bin
	objdump --disassembler-color=on -M intel -D -b binary -mi386 -Maddr32,data32 $< | less -R

# Stage 0, runs in low memory (KERNEL_PHYS_ADDR)
_linker.ld: linker.ld stage0.o mman.o
	$(CC) -E -P -x c $(SHARED_DEFINES) $< >$@

KERNEL_OBJS=\
	stage1.o \
	terminal.o \
	idt.o \
	keyboard.o \
	mm/vma.o \
	serial_debug.o \
	utils.o \
	pit.o \
	timer.o \
	chardev.o \
	mm/pfa.o \
	lib/v_impl.o \
	panic.o \
	sched.o \
	console.o \
	x86.o \
	initcalls.o \
	driver.o \
	drivers/floppy.o \
	drivers/cmos.o \
	syscalls.o \
	kprintf.o \
	dma.o \
	fs/block.o \
	sync.o \
	lib/ringbuf.o \
	kmalloc.o \

KERNEL_DEPS=stage0.o $(KERNEL_OBJS)

KERNEL_DEPFILES=$(KERNEL_DEPS:%.o=%.d)
-include $(KERNEL_DEPFILES)

kernel.bin: $(KERNEL_DEPS) _linker.ld
	$(LD) -T _linker.ld -melf_i386 --build-id=none -z norelro -z now -o $@ $(KERNEL_OBJS)
	$(OBJCOPY) --only-keep-debug "$@" "$@.dbg"
	$(STRIP) --strip-debug --strip-unneeded "$@"
	$(OBJCOPY) --add-gnu-debuglink="$@.dbg" "$@"

od: kernel.bin
	objdump --disassembler-color=on -M intel -d $< -WK | less

kernel.bin.dbg: kernel.bin

kernel.sym: kernel.bin.dbg
	nm $^ | grep ' [tT] ' | awk '{print $$1" "$$3}' > $@

%720.bin: %.S Makefile kernel.bin
	$(AS) $(ASFLAGS) -DSECTORS_PER_TRACK=9 -DKIMG_SECTORS=$$(scripts/get_sectors.sh kernel.bin) -f bin -o $@ $<

%1440.bin: %.S Makefile kernel.bin
	$(AS) $(ASFLAGS) -DSECTORS_PER_TRACK=18 -DKIMG_SECTORS=$$(scripts/get_sectors.sh kernel.bin) -f bin -o $@ $<

720k.img: mbr720.bin kernel.bin
	cat $^ > $@
	truncate -s 720k $@

1440k.img: mbr1440.bin kernel.bin
	cat $^ > $@
	truncate -s 1440k $@

HXCFE=~/git/HxCFloppyEmulator/build/hxcfe
DTC=~/vintage/utils/dtc-3.5.0/bin/dtc
kryohd: 1440k.img
	mkdir -p flux
	$(HXCFE) -finput:"$<" -conv:KRYOFLUXSTREAM -foutput:"flux/track"
	# $(DTC) -f"flux/track*" -s0 -e7 -w
	$(DTC) -f"flux/track*" -s0 -e$$(scripts/get_cyls.sh 18 kernel.bin) -w

kryodd: 720k.img
	mkdir -p flux
	$(HXCFE) -finput:"$<" -conv:KRYOFLUXSTREAM -foutput:"flux/track"
	$(DTC) -f"flux/track*" -s0 -e$$(scripts/get_cyls.sh 9 kernel.bin) -w

clean_kryo:
	rm -rf flux

media: 720k.img 1440k.img

clean: clean_kryo
	rm -f kernel.bin* mbr*.bin *k.img kernel.sym _linker.ld $(KERNEL_DEPFILES) $(KERNEL_DEPS)
