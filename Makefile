all: run-qemu

run-qemu: toolchain/local
	make -C kernel qemu

toolchain/local:
	make -C toolchain

clean: clean-kernel

clean-kernel:
	make -C kernel clean
