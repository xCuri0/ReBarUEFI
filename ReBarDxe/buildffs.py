#!/usr/bin/env python3
import os
import sys
import glob
import subprocess
import glob

name = "ReBarDxe"
version = "1.0"
GUID = "a8ee1777-a4f5-4345-9da4-13742084d31e"
shell = sys.platform == "win32"
buildtype = "RELEASE"


def filesub(filep, f, r):
    # Read in the file
    with open(filep, 'r') as file :
        filedata = file.read()

    # Replace the target string
    filedata = filedata.replace(f, r)

    # Write the file out again
    with open(filep, 'w') as file:
        file.write(filedata)

if len(sys.argv) > 1:
    buildtype = sys.argv[1].upper()

# 3 arguments = Github Actions
if len(sys.argv) == 3:
    print("TARGET: ", os.environ['TARGET'])
    print("TARGET_ARCH: ", os.environ['TARGET_ARCH'])
    print("TOOL_CHAIN_TAG: ", os.environ['TOOL_CHAIN_TAG'])

    # setup Conf/target.txt
    filesub("./Conf/target.txt", "DEBUG", os.environ['TARGET'])
    filesub("./Conf/target.txt", "IA32", os.environ['TARGET_ARCH'])
    filesub("./Conf/target.txt", "VS2015x86", os.environ['TOOL_CHAIN_TAG'])
else:
    os.chdir("../..")

subprocess.run(["build", "--platform=ReBarUEFI/ReBarDxe/ReBar.dsc"], shell=shell, env=os.environ, stderr=sys.stderr, stdout=sys.stdout)

ReBarDXE = glob.glob(f"./Build/ReBarUEFI/{buildtype}_*/X64/ReBarDxe.efi")

if len(ReBarDXE) != 1:
    print("Build failed")
    sys.exit(1)

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