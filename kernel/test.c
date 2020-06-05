#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

int places(uint64_t n) {
	if (n < 10) return 1;
	return 1 + places(n / 10);
}

void terminal_writenum(uint64_t num) {
	//We've got to marshal this number here to build up a string.
	//No floating point yet.
	uint8_t len = places(num);
	char buf[len + 1];

	for (int i = 0; i < len; ++i) {
		uint64_t remainder = num % 10;
		num /= 10;
		buf[len - i - 1] = remainder + '0';
	}
	//buf[len + 1] = '\0';
	printf("%s", buf);
	printf("\n");
}

int main(int argc, char **argv) {
	terminal_writenum(123);
	return 0;
}
