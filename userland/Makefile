LD=../toolchain/local/bin/i686-pc-minima-ld
BIN=../fsroot/bin

BINARIES=\
	$(BIN)/hello

all: $(BINARIES)

hello.o: hello.S
	nasm -f elf32 -o $@ $<

$(BIN)/hello: hello.o
	$(LD) $< -o $@

clean:
	rm -f *.o ../fsroot/bin/*
