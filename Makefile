all: run-qemu

run-qemu: toolchain/local
	make -C kernel qemu

toolchain/local:
	make -C toolchain

.PHONY: toolchain
toolchain: toolchain/local

clean: clean-kernel

clean-kernel:
	make -C kernel clean
