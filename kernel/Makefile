CC=../toolchain/local/bin/i686-pc-minima-gcc
CFLAGS=-g -std=gnu99 -ffreestanding -O0 -Wall -Wextra
QEMU=qemu-system-i386

all: minima.bin

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

clean:
	rm *.o minima.bin
