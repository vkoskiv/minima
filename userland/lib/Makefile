# TOOLCHAIN=../../toolchain/local/bin

all: libc.a

DEPS=\
	crt.o \
	c/stdio.o \
	c/unistd.o \

libc.a: $(DEPS)
	$(AR) rs "$@" $^

clean:
	rm -f *.o *.d *.a
