#!/bin/bash
scp minima.img root@192.168.1.105:
ssh root@192.168.1.105 -t dd if=minima.img of=/dev/fd0 bs=512 status=progress
