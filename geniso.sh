#!/bin/bash

umount ./uefimount
rm uefi.iso
mkfs.msdos -C ./uefi.iso 2880
mount uefi.iso ./uefimount/
mkdir -p ./uefimount/EFI/boot/
cp ../../../Shell.efi ./uefimount/EFI/boot/bootx64.efi
cp ../Build/ReBarUEFI/DEBUG_GCC5/X64/ReBarDxe.efi ./uefimount/
sleep 1
umount ./uefimount
