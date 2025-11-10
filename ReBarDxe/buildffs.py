#!/usr/bin/env python3
#
# Copyright (c) 2022 xCuri0 <zkqri0@gmail.com>
# SPDX-License-Identifier: MIT
#
import os
import sys
import glob
import subprocess
import glob
from pefile import PE

name = "ReBarDxe"
version = "1.0"
GUID = "a8ee1777-a4f5-4345-9da4-13742084d31e"
shell = sys.platform == "win32"
buildtype = "RELEASE"


def target_update(filep, p, v):
    # Read in the file
    with open(filep, 'r') as file :
        lines = file.read()

    # Write the file out again
    with open(filep, 'w') as file:
        for i, l in enumerate(lines.splitlines()):
            if l.split('=')[0].strip() == p:
                file.write(f"{p} = {v}\n")
            else:
                file.write(f"{l.rstrip()}\n")

def set_bit(data, bit):
    """Sets a specific bit."""
    return data | (1 << bit)

def set_nx_compat_flag(pe):
    """Sets the nx_compat flag to 1 in the PE/COFF file."""
    dllchar = pe.OPTIONAL_HEADER.DllCharacteristics
    dllchar = set_bit(dllchar, 8)  # 8th bit is the nx_compat_flag
    pe.OPTIONAL_HEADER.DllCharacteristics = dllchar
    pe.merge_modified_section_data()
    return pe

if len(sys.argv) > 1:
    buildtype = sys.argv[1].upper()

# 3 arguments = Github Actions
if len(sys.argv) == 3:
    print("TARGET: ", os.environ['TARGET'])
    print("TARGET_ARCH: ", os.environ['TARGET_ARCH'])
    print("TOOL_CHAIN_TAG: ", os.environ['TOOL_CHAIN_TAG'])

    # setup Conf/target.txt
    target_update("./Conf/target.txt", "TARGET", os.environ['TARGET'])
    target_update("./Conf/target.txt", "TARGET_ARCH", os.environ['TARGET_ARCH'])
    target_update("./Conf/target.txt", "TOOL_CHAIN_TAG", os.environ['TOOL_CHAIN_TAG'])
else:
    os.chdir("../..")

subprocess.run(["build", "--platform=ReBarUEFI/ReBarDxe/ReBar.dsc"], shell=shell, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)

ReBarDXE = glob.glob(f"./Build/ReBarUEFI/{buildtype}_*/X64/ReBarDxe.efi")

if len(ReBarDXE) != 1:
    print("Build failed")
    sys.exit(1)

# set NX_COMPAT
pe = PE(ReBarDXE[0])
set_nx_compat_flag(pe)

os.remove(ReBarDXE[0])
pe.write(ReBarDXE[0])

print(ReBarDXE[0])
print("Building FFS")
os.chdir(os.path.dirname(ReBarDXE[0]))

try:
    os.remove("pe32.sec")
    os.remove("name.sec")
    os.remove("ReBarDxe.ffs")
except FileNotFoundError:
    pass

subprocess.run(["GenSec", "-o", "pe32.sec", "ReBarDxe.efi", "-S", "EFI_SECTION_PE32"], shell=shell, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)
subprocess.run(["GenSec", "-o", "name.sec", "-S", "EFI_SECTION_USER_INTERFACE", "-n", name], shell=shell, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)
subprocess.run(["GenFfs", "-g", GUID, "-o", "ReBarDxe.ffs", "-i", "pe32.sec", "-i" ,"name.sec", "-t", "EFI_FV_FILETYPE_DRIVER", "--checksum"], shell=shell, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)

try:
    os.remove("pe32.sec")
    os.remove("name.sec")
except FileNotFoundError:
    pass

print("Finished")