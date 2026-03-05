#!/bin/bash
#
# Calculate cylinder count to minimize floppy writing time
#
# This assumes:
# 512 bytes per sector
# 2 sides
#
if [ $# -lt 2 ]; then
	echo "Usage: $0 <sectors_per_track> <image_file>"
	exit 1
fi

SECT_BYTES=512
SEC_PER_TRACK="$1"
SEC_PER_CYL=$(( 2 * $SEC_PER_TRACK ))
IMG="$2"

TOTAL_BYTES=$(du -sb "$IMG" | awk '{ print $1 }')

# Add boot sector to size
TOTAL_BYTES=$(( TOTAL_BYTES + 512 ))

SECTORS=$(( TOTAL_BYTES / SECT_BYTES ))
CYLS=$(( ($SECTORS + ($SEC_PER_CYL - 1)) / $SEC_PER_CYL ))
echo -n "$CYLS"
# echo "total_bytes = $TOTAL_BYTES, sectors = $SECTORS, cyls = $CYLS"
