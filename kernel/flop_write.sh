#!/bin/bash
IP=192.168.1.107
USER=sysv
scp minima.img "$USER"@"$IP":
ssh "$USER"@"$IP" -t dd if=minima.img of=/dev/fd0 bs=512 status=progress
