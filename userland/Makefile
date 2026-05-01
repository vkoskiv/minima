LD=../toolchain/local/bin/i686-pc-minima-ld
BIN=../fsroot/bin

TOOLCHAIN=../toolchain/local/bin
TRIPLE=i686-pc-minima-
CC=$(TOOLCHAIN)/$(TRIPLE)gcc
LD=$(TOOLCHAIN)/$(TRIPLE)ld
OBJCOPY=$(TOOLCHAIN)/$(TRIPLE)objcopy
STRIP=$(TOOLCHAIN)/$(TRIPLE)strip
AS=nasm
OPT_LEVEL=-O0
WARNFLAGS=-Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-missing-braces
CFLAGS=$(SHARED_DEFINES) -masm=intel -I../kernel/include -ggdb3 -std=gnu99 -fno-omit-frame-pointer -ffreestanding -march=i486 $(OPT_LEVEL) $(WARNFLAGS) -nostdlib -lgcc

BINARIES=\
	$(BIN)/hello \
	$(BIN)/sh

all: $(BINARIES)

hello.o: hello.S
	nasm -f elf32 -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

$(BIN)/hello: hello.o
	$(LD) $< -o $@
$(BIN)/sh: sh.o lib.o
	$(LD) $^ -o $@

clean:
	rm -f *.o *.d ../fsroot/bin/*
