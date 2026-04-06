# minima

My hobby kernel for x86 PCs running a 486 or newer.

## Features

- Custom 16-bit boot sector bootloader (`mbr.S`)
- Memory management
  - `mm/pfa.c`: physical memory allocator, based on a lock-free freelist. Provides `pf_alloc()` and `pf_free()` to manage fixed `PAGE_SIZE` (4096B) allocations.
  - `mm/vma.c`: virtual memory manager, enabling virtually contiguous allocations of >4096 bytes. Single virtual address space for now, with some logic to defragment it occasionally.
  - `mm/slab.c`: my take on a slab allocator. Backed by `pf_[alloc,free]()`, used for all allocations of <= 4096 bytes.
- Scheduler
  - preemptive, round-robin, with priority.
  - Basic support for sleep() (deadline + yield())
  - Several kernel threads to manage things:
    - `kidle`: idle task
    - `kreaper`: task teardown
    - `kvmdefrag`: kernel virtual address space defragmenter
    - `kserial`: flush serial ringbuffer
- Sync primitives:
  - Semaphore, based on 80486 atomic `lock cmpxchg`.
    - Pending tasks may sleep, and are awoken one at a time for each `sem_post()` call.
- Drivers
  - `drivers/floppy.c`: 8272A/82078 floppy controller driver
    - Tested on bare metal, fairly reliably reads 1.44M, 720k 3.5" disks, as well as 360k/1.2M 5.25" disks.
    - LRU track (cylinder) cache to speed up random reads
    - Media change (eject) detection
    - Media type detection
- Filesystems
  - ext2
    - Early days still. Can traverse the filesystem tree and read files, but not much else yet.
- Misc. platform support code (x86/PC standard only for now)
  - 8259 (interrupts)
  - 8042 (keyboard)

Many of these implementations are likely far from optimal, since they are primarily derived from first principles based on high-level descriptions found on e.g. Wikipedia. and my existing knowledge. Implementations will improve over time, as needed, of course.

## Goals

I'd like to load and execute the ELF binary of some non-trivial free software program, loaded off a ext2 filesystem on the boot disk. I think I'd be pretty happy with this project if I could do that, and maybe have a little home-grown shell + userland as well. TBD when that happens, but I'm having a lot of fun hacking on the kernel in the meantime! :]

## Requirements

- CPU: 40486 or newer CPU with atomic `cmpxchg` instruction support
- Memory: 640k should be enough, but some tests may assume more memory is available.

## Building & Running

Install some dependencies first. These should cover most of it, but YMMV:
`pacman -S gcc binutils mpfr mpc gmp qemu`

In project root, run `make toolchain` to fetch and build the cross-compiling toolchain. This takes a few minutes, but you only have to do it once. Once that's done, `cd kernel` and `make` to run the system under `qemu`.

Most development happens under the `kernel/` subdirectory, here are some useful `make(1)` targets I use quite often:

`make qemu`: build & run system under QEMU
`make reload T=qemu`: Depends on `entry(1)`, install that if you haven't already. Runs the target specified with `T=` whenever a source file is modified. Useful for trying things out.
`make od`: Shows `objdump(1)` of kernel binary
`make media`: generate disk images, ready for writing to a floppy disk.

## References used:

- `Intel 80386 Programmer's Reference Manual 1986`
- [`Intel 82078 datasheet`](https://wiki.qemu.org/images/f/f0/29047403.pdf)
- [`Intel 8272A datasheet`](https://www.threedee.com/jcm/terak/docs/Intel%208272A%20Floppy%20Controller.pdf)
- [felixcloutier.com x86 reference](https://www.felixcloutier.com/x86/)
- [Wikipedia](https://wikipedia.org)
- [various OSDev wiki pages](https://wiki.osdev.org)
