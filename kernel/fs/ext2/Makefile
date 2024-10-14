blockdev.o: blockdev.c blockdev.h
	cc -c blockdev.c -o blockdev.o

btest_main.o: btest_main.c
	cc -c btest_main.c -o btest_main.o

btest : btest_main.o blockdev.o
	cc -o btest -O0 -g btest_main.o blockdev.o

ext2.o : ext2.c
	cc -c ext2.c -o ext2.o

ext2_test.o : ext2_test.c
	cc -c ext2_test.c -o ext2_test.o

ext : ext2_test.o ext2.o blockdev.o
	cc -o ext -O0 -g ext2_test.o ext2.o blockdev.o
