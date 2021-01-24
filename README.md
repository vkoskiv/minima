## The minima Operating System

Right now, this is just a few lines of C to print some text. The biggest part of it right now is the toolchain, which makes getting started easy. Just run one script and you're done!


# Before running:
- Install qemu-system-i386 and verify it's in your $PATH
- Have a recent-ish GCC/clang on your system

# To run:
First, head to toolchain/ and run buildtoolchain.sh to build the cross-compiler and other needed components.
This may take a few minutes, it builds a copy of GCC & friends from scratch.

Then head to `kernel/` and do `make run` to compile and run the kernel in QEMU.
You can also just `make` to compile, without running.
