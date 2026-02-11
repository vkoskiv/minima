CC=../toolchain/local/bin/i686-pc-minima-gcc
LD=../toolchain/local/bin/i686-pc-minima-ld
AS=nasm
# TODO: -masm=intel & update all inline asm
CFLAGS=-Iinclude/ -ggdb3 -std=gnu99 -ffreestanding -O0 -Wall -Wextra -nostdlib -lgcc
QEMU=qemu-system-i386
QEMUFLAGS=-no-reboot -display gtk,show-menubar=off -m 32 -fda $^

all: bochs

bothi:
	ls Makefile *.S *.c *.h | entr -r sh -c 'make qemu bochs'

qemu: minima.img
	$(QEMU) $(QEMUFLAGS) -debugcon stdio

qemui:
	ls Makefile *.S *.c *.h | entr -r sh -c 'make qemu'

debug: minima.img
	$(QEMU) $(QEMUFLAGS) -d int,pcall,cpu_reset,guest_errors,invalid_mem,mmu -debugcon stdio

gdb: minima.img
	gdb kernel.bin -ex "b panic" -ex "target remote | exec $(QEMU) $(QEMUFLAGS) -gdb stdio"

bochs: minima.img kernel.sym
	BXSHARE=/home/vkoskiv/git/Bochs/bochs/bios/ /home/vkoskiv/git/Bochs/bochs/bochs -f .bochsrc -q -rc .bochs_cmds

bochsi:
	ls Makefile *.S *.c *.h | entr -r sh -c 'make bochs'

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $^

# new NASM files
%.o: %.S
	$(AS) -f elf32 $<

%.bin: %.S
	$(AS) -f bin -o $@ $<

# old GAS files
%.o: %.s
	$(CC) -c $^

wrflop: minima.img
	./flop_write.sh

dumpmbr: mbr.bin
	objdump --disassembler-color=on -M intel -D -b binary -mi386 -Maddr16,data16 $< | less -R

# Stage 0, runs in low memory (0x00010000)
linker.ld: stage0.o mman.o stage0_asm.o

KERNEL_OBJS=\
	kernel.o \
	terminal.o \
	assert.o \
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

KERNEL_DEPS=linker.ld $(KERNEL_OBJS)

kernel.bin: $(KERNEL_DEPS)
	$(LD) -T linker.ld -melf_i386 --gc-sections --build-id=none -z norelro -z now -o $@ $(KERNEL_OBJS)
	objcopy --only-keep-debug "$@" "$@.dbg"
	strip --strip-debug --strip-unneeded "$@"
	objcopy --add-gnu-debuglink="$@.dbg" "$@"

kernel.bin.dbg: kernel.bin

kernel.sym: kernel.bin.dbg
	nm $^ | grep ' T ' | awk '{print $$1" "$$3}' > $@

minima.img: mbr.bin kernel.bin
	cat mbr.bin kernel.bin > $@
	truncate -s 1440k $@


clean:
	rm -f *.o kernel.bin* mbr.bin minima.img kernel.sym
