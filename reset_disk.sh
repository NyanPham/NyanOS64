#!/bin/bash

rm -f hdd.img
dd if=/dev/zero of=hdd.img bs=1M count=64
mkfs.fat -F 32 -n "DATA_DISK" hdd.img
echo "Reset the disk hdd.img successfully!"