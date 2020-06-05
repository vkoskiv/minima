#!/bin/bash
../toolchain/local/bin/i686-pc-minima-gcc -c boot.s \
&& \
../toolchain/local/bin/i686-pc-minima-gcc -c kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
&& \
../toolchain/local/bin/i686-pc-minima-gcc -T linker.ld -o minima.bin -ffreestanding -O2 -nostdlib boot.o kernel.o -lgcc
