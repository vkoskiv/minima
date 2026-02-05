CC=../toolchain/local/bin/i686-pc-minima-gcc
LD=../toolchain/local/bin/i686-pc-minima-ld
# TODO: -masm=intel & update all inline asm
CFLAGS=-g -std=gnu99 -ffreestanding -O0 -Wall -Wextra -nostdlib -lgcc
QEMU=qemu-system-i386
QEMUFLAGS=-m 32 -fda $^

all: bochs

qemu: minima.img
	$(QEMU) $(QEMUFLAGS) -debugcon stdio

qemui:
	ls *.c *.h | entr -r sh -c 'make qemu'

debugint: minima.img
	$(QEMU) $(QEMUFLAGS) -d int -monitor stdio

gdb: minima.img
	gdb kernel.bin -ex "b panic" -ex "target remote | exec $(QEMU) $(QEMUFLAGS) -gdb stdio"

bochs: minima.img kernel.sym
	BXSHARE=/home/vkoskiv/git/Bochs/bochs/bios/ /home/vkoskiv/git/Bochs/bochs/bochs -f .bochsrc -q -rc .bochs_cmds

bochsi:
	ls *.c *.h | entr -r sh -c 'make bochs'

irq.o: irq.s
	$(CC) -c $^

paging.o: paging.s
	$(CC) -c $^

gdt.o: gdt.s
	$(CC) -c $^

interrupt_wrapper.o: interrupt_wrapper.s
	$(CC) -c $^

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $^

wrflop: minima.img
	./flop_write.sh

mbr.bin: flopboot.s
	nasm -f bin -o mbr.bin flopboot.s
	
dumpmbr: mbr.bin
	objdump --disassembler-color=on -M intel -D -b binary -mi386 -Maddr16,data16 $< | less -R

kernel.bin: _start.o kernel.o terminal.o assert.o idt.o irq.o irq_handlers.o keyboard.o mman.o paging.o gdt.o serial_debug.o utils.o
	$(LD) -T linker.ld --strip-debug -melf_i386 --gc-sections --build-id=none -z norelro -z now -o $@ kernel.o terminal.o assert.o idt.o irq.o irq_handlers.o keyboard.o mman.o paging.o gdt.o serial_debug.o utils.o

kernel.sym: kernel.bin
	nm $^ | grep ' T ' | awk '{print $$1" "$$3}' > $@

minima.img: mbr.bin kernel.bin
	cat mbr.bin kernel.bin > $@
	truncate -s 1440k $@


clean:
	rm -f *.o kernel.bin mbr.bin minima.img kernel.sym
