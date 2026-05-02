BIN=../fsroot/bin

export ROOTDIR=${PWD}
TOOLCHAIN=$(ROOTDIR)/../toolchain/local/bin
TRIPLE=i686-pc-minima-
CC=$(TOOLCHAIN)/$(TRIPLE)gcc
LD=$(TOOLCHAIN)/$(TRIPLE)ld
OBJCOPY=$(TOOLCHAIN)/$(TRIPLE)objcopy
STRIP=$(TOOLCHAIN)/$(TRIPLE)strip
AS=nasm
OPT_LEVEL=-O0
WARNFLAGS=-Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-missing-braces
CFLAGS=$(SHARED_DEFINES) -masm=intel -I -isystem $(ROOTDIR)/../kernel/include/user --sysroot=$(ROOTDIR)/sysroot -ggdb3 -std=gnu99 -fno-omit-frame-pointer -ffreestanding -march=i486 $(OPT_LEVEL) $(WARNFLAGS) -nostdlib -lgcc

BINARIES=\
	$(BIN)/hello \
	$(BIN)/sh

all: $(BINARIES)

hello.o: hello.S
	nasm -f elf32 -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

LIBC=lib/libc.a
export
# FIXME: I haven't the faintest clue why this needs to be
# PHONY, it shouldn't need that.
.PHONY: $(LIBC)
$(LIBC):
	make -C lib

$(BIN)/hello: hello.o $(LIBC)
	$(LD) $< -o $@
$(BIN)/sh: sh.o $(LIBC)
	$(LD) $^ -o $@

clean:
	rm -f *.o *.d ../fsroot/bin/*
	make -C lib clean
