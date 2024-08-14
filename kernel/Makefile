CC=../toolchain/local/bin/i686-pc-minima-gcc
LD=../toolchain/local/bin/i686-pc-minima-ld
CFLAGS=-g -std=gnu99 -ffreestanding -O0 -Wall -Wextra -nostdlib -lgcc
QEMU=qemu-system-i386

all: minima.bin

grub: minima.bin
	./update_image.sh

rungrub: grub
	$(QEMU) -debugcon stdio minima.img

run: minima.bin
	$(QEMU) -debugcon stdio -kernel minima.bin

debugint: minima.bin
	$(QEMU) -d int -monitor stdio -kernel minima.bin

gdb: minima.bin
	$(QEMU) -s -S -monitor stdio -kernel minima.bin

boot.o: boot.s
	$(CC) -c $^
	
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

minima.bin: boot.o kernel.o terminal.o assert.o idt.o irq.o irq_handlers.o keyboard.o mman.o paging.o multiboot.o gdt.o serial_debug.o utils.o
	$(CC) -T linker.ld -o $@ $(CFLAGS) -nostdlib $^ -lgcc

wrflop: minima.img
	./flop_write.sh

minima.img: mbr.bin kernel.bin
	cat mbr.bin kernel.bin > minima.img

# Bochs wants the disk to be the full 1.44MB and will reject the image otherwise, so pad that out here
bochs.img: minima.img
	dd if=/dev/zero bs=1440k count=1 conv=notrunc > bochs.img 2> /dev/null
	dd if=minima.img of=bochs.img bs=1440k count=1 conv=notrunc 2>/dev/null

mbr.bin: flopboot.s
	nasm -f bin -o mbr.bin flopboot.s
	
kernel.bin: _start.o kernel.o terminal.o assert.o idt.o irq.o irq_handlers.o keyboard.o mman.o paging.o multiboot.o gdt.o serial_debug.o utils.o
	$(LD) -T linker.ld --strip-debug -melf_i386 --gc-sections --build-id=none -z norelro -z now -o $@ kernel.o terminal.o assert.o idt.o irq.o irq_handlers.o keyboard.o mman.o paging.o multiboot.o gdt.o serial_debug.o utils.o
	
clean:
	rm -f *.o minima.bin kernel.bin mbr.bin minima.img
