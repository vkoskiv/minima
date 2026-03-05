#!/bin/bash
#
# Calculate sectors for bootloader load routine
#
# This assumes:
# 512 bytes per sector
# 2 sides
#
if [ $# -lt 1 ]; then
	echo "Usage: $0 <image_file>"
	exit 1
fi

SECT_BYTES=512
IMG="$1"

TOTAL_BYTES=$(du -sb "$IMG" | awk '{ print $1 }')

# NOTE: Bootloader already skips the boot sector in loading routine
SECTORS=$(( (TOTAL_BYTES + ($SECT_BYTES - 1)) / SECT_BYTES ))
echo -n "$SECTORS"
