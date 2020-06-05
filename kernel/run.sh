#!/bin/bash
./build.sh
qemu-system-i386 -kernel minima.bin
