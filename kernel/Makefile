CC=../toolchain/local/bin/i686-pc-minima-gcc
CFLAGS=-std=gnu99 -ffreestanding -O2 -Wall -Wextra
QEMU=qemu-system-i386

all: minima.bin

run: minima.bin
	$(QEMU) -kernel minima.bin

boot.o: boot.s
	$(CC) -c $^
	
irq.o: irq.s
	$(CC) -c $^

interrupt_wrapper.o: interrupt_wrapper.s
	$(CC) -c $^

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $^

minima.bin: boot.o kernel.o terminal.o assert.o interrupt_wrapper.o interrupt_handler.o idt.o irq.o irq_handlers.o keyboard.o mman.o multiboot.o
	$(CC) -T linker.ld -o $@ $(CFLAGS) -nostdlib $^ -lgcc

clean:
	rm *.o minima.bin
