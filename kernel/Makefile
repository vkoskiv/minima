# Constants shared by bootloader, kernel code & linker script.
SHARED_DEFINES=\
	-DSTACK_TOP=0x4000 \
	-DSTACK_SIZE=0x4000 \
	-DKERNEL_PHYS_ADDR=0x8000 \
	-DVIRT_OFFSET=0xC0000000 \
	-DPFA_VIRT_OFFSET=0xD0000000 \

CC=../toolchain/local/bin/i686-pc-minima-gcc
LD=../toolchain/local/bin/i686-pc-minima-ld
AS=nasm
# TODO: -masm=intel & update all inline asm
CFLAGS=$(SHARED_DEFINES) -Iinclude/ -ggdb3 -std=gnu99 -ffreestanding -O0 -Wall -Wextra -nostdlib -lgcc
ASFLAGS=$(SHARED_DEFINES)
QEMU=qemu-system-i386
QEMUFLAGS=-no-reboot -display gtk,show-menubar=off -m 32 -fda $^

all: qemu

bothi:
	ls Makefile *.S *.c *.h | entr -r sh -c 'make qemu bochs'

qemu%: %k.img
	$(QEMU) $(QEMUFLAGS) -debugcon stdio

qemu: qemu1440

qemui:
	ls Makefile *.S *.c *.h | entr -r sh -c 'make qemu'

debug: 1440k.img
	$(QEMU) $(QEMUFLAGS) -d int,pcall,cpu_reset,guest_errors,invalid_mem,mmu -debugcon stdio

gdb: 1440k.img
	gdb kernel.bin -ex "b halt" -ex "target remote | exec $(QEMU) $(QEMUFLAGS) -gdb stdio"

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

# old GAS files
%.o: %.s
	$(CC) -c $^

wrflop%: %k.img
	./flop_write.sh $<

wrflop: wrflop1440

dumpmbr: mbr1440.bin
	objdump --disassembler-color=on -M intel -D -b binary -mi386 -Maddr16,data16 $< | less -R

# Stage 0, runs in low memory (KERNEL_PHYS_ADDR)
_linker.ld: linker.ld stage0.o mman.o stage0_asm.o
	$(CC) -E -P -x c $(SHARED_DEFINES) $< >$@

KERNEL_OBJS=\
	stage1.o \
	terminal.o \
	idt.o \
	irq.o \
	irq_handlers.o \
	keyboard.o \
	mman.o \
	serial_debug.o \
	utils.o \
	pit.o \
	timer.o \
	chardev.o \
	pfa.o \
	v_impl.o \
	panic.o \

KERNEL_DEPS=stage0.o stage0_asm.o $(KERNEL_OBJS)

KERNEL_DEPFILES=$(KERNEL_DEPS:%.o=%.d)
-include $(KERNEL_DEPFILES)

kernel.bin: $(KERNEL_DEPS) _linker.ld
	$(LD) -T _linker.ld -melf_i386 --gc-sections --build-id=none -z norelro -z now -o $@ $(KERNEL_OBJS)
	objcopy --only-keep-debug "$@" "$@.dbg"
	strip --strip-debug --strip-unneeded "$@"
	objcopy --add-gnu-debuglink="$@.dbg" "$@"

kernel.bin.dbg: kernel.bin

kernel.sym: kernel.bin.dbg
	nm $^ | grep ' T ' | awk '{print $$1" "$$3}' > $@

%720.bin: %.S Makefile
	$(AS) $(ASFLAGS) -DSECTORS_PER_TRACK=9 -f bin -o $@ $<

%1440.bin: %.S Makefile
	$(AS) $(ASFLAGS) -DSECTORS_PER_TRACK=18 -f bin -o $@ $<

720k.img: mbr720.bin kernel.bin
	cat $^ > $@
	truncate -s 720k $@

1440k.img: mbr1440.bin kernel.bin
	cat $^ > $@
	truncate -s 1440k $@

media: 720k.img 1440k.img

clean:
	rm -f kernel.bin* mbr*.bin *k.img kernel.sym _linker.ld $(KERNEL_DEPFILES) $(KERNEL_DEPS)
