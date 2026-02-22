#!/bin/bash
if [ $# -lt 1 ]; then
	echo "Usage: $0 <disk_img>"
	exit 1
else
	IP=192.168.1.107
	USER=sysv
	scp "$1" "$USER"@"$IP":
	ssh "$USER"@"$IP" -t dd if="$1" of=/dev/fd0 bs=512 status=progress
fi
