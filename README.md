## The minima Operating System

Right now, this is just a few lines of C to print some text. The biggest part of it right now is the toolchain, which makes getting started easy. Just run one script and you're done!

# Before running:
## Install prerequisites:
### On linux:
  `sudo apt install build-essentials curl libmpfr-dev libmpc-dev libgmp-dev qemu-system-i386 qemu-utils`
### On macOS:
  `brew install coreutils qemu m4 autoconf libtool automake bash gcc@10`

# To run:
First, head to toolchain/ and run buildtoolchain.sh to build the cross-compiler and other needed components.
This may take a few minutes, it builds a copy of GCC & friends from scratch. You only need to build this once in a while, it's reused unless large changes are made.

Then head to `kernel/` and do `make run` to compile and run the kernel in QEMU.
You can also just `make` to compile, without running.
