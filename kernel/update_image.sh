#!/bin/bash

# Create or update an existing grub image.
# This image can just be dd'd to a physical disk to
# try out on bare metal
# TODO: Make these params adjustable, maybe.

set -eux

IMGNAME=minima

create_image() {
  
  losetup -D

  # 64MB image
  dd if=/dev/zero of="$IMGNAME".img bs=512 count=131072

  # https://superuser.com/questions/332252/how-to-create-and-format-a-partition-using-a-bash-script
  sed -e 's/\s*\([\+0-9a-zA-Z]*\).*/\1/' << EOF | fdisk "$IMGNAME".img
    n # New partition
    p # Primary
    1 # Partition number 1
    2048 # Start sector
    131071 # End sector
    w # Write table
EOF

  # Then all the complex grub setup that I forget already :(
  losetup /dev/loop0 "$IMGNAME".img
  losetup /dev/loop1 "$IMGNAME".img -o 1048576
  mkfs.ext2 /dev/loop1
  sudo mount /dev/loop1 mnt
  sudo mkdir -p mnt/boot/grub
  sudo bash -c 'cat << EOF >mnt/boot/grub/grub.cfg
  menuentry "minima" {
    multiboot /boot/minima.bin
  }
EOF'
  sudo bash -c 'cat << EOF >mnt/boot/grub/device.map
  (hd0)   /dev/loop0
  (hd0,1) /dev/loop1
EOF'

  sudo grub-install --boot-directory=mnt/boot --target=i386-pc --allow-floppy --modules="normal part_msdos ext2 multiboot" /dev/loop0
  sudo cp minima.bin mnt/boot/
  sudo umount mnt
  losetup -D
}

if [ ! -f "$IMGNAME".img ]; then
  echo "Creating a fresh new image"
  create_image
else
  echo "Updating existing image"
  losetup /dev/loop0 "$IMGNAME".img
  losetup /dev/loop1 "$IMGNAME".img -o 1048576
  sudo mount /dev/loop1 mnt
  sudo cp minima.bin mnt/boot/
  sudo umount mnt
  losetup -D
fi
