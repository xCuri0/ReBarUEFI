#!/usr/bin/env python3
import os
import sys
import glob
import subprocess
import glob

name = "ReBarDxe"
version = "1.0"
GUID = "a8ee1777-a4f5-4345-9da4-13742084d31e"

os.chdir("..")
subprocess.run(["build", "--platform=ReBarUEFI/ReBar.dsc", "--arch=X64", "--buildtarget=RELEASE"], shell=True, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)

ReBarDXE = glob.glob("./Build/ReBarUEFI/RELEASE_*/X64/ReBarDxe.efi")

if len(ReBarDXE) != 1:
    print("Build failed")
    sys.exit(1)

print("Building FFS")

os.chdir(os.path.dirname(ReBarDXE[0]))

try:
    os.remove("pe32.sec")
    os.remove("name.sec")
    os.remove("ReBarDxe.ffs")
except FileNotFoundError:
    pass

subprocess.run(["GenSec", "-o", "pe32.sec", "ReBarDxe.efi", "-S", "EFI_SECTION_PE32"], shell=True, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)
subprocess.run(["GenSec", "-o", "name.sec", "-S", "EFI_SECTION_USER_INTERFACE", "-n", name], shell=True, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)
subprocess.run(["GenFfs", "-g", GUID, "-o", "ReBarDxe.ffs", "-i", "pe32.sec", "-i" ,"name.sec", "-t", "EFI_FV_FILETYPE_DRIVER"], shell=True, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)

try:
    os.remove("pe32.sec")
    os.remove("name.sec")
except FileNotFoundError:
    pass

print("Finished")