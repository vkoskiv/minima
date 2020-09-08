#!/bin/bash
./build.sh || exit
qemu-system-i386 -kernel minima.bin
