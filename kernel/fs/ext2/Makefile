CFLAGS=-O0 -g
blockdev.o: blockdev.c blockdev.h
	cc $(CFLAGS) -c blockdev.c -o blockdev.o

btest_main.o: btest_main.c
	cc $(CFLAGS) -c btest_main.c -o btest_main.o

btest : btest_main.o blockdev.o
	cc $(CFLAGS) -o btest btest_main.o blockdev.o

ext2.o : ext2.c
	cc $(CFLAGS) -c ext2.c -o ext2.o

ext2_test.o : ext2_test.c
	cc $(CFLAGS) -c ext2_test.c -o ext2_test.o

ext : ext2_test.o ext2.o blockdev.o
	cc $(CFLAGS) -lm -o ext ext2_test.o ext2.o blockdev.o

exttest : ext
	./ext
