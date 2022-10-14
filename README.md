# ReBarUEFI
DXE driver to enable resizable BAR on systems which don't support it officially.

### Requirements
* PCIe 3.0 (Ivy Bridge+)
* 4G Decoding enabled
* BIOS support for Large BARs (often limited to 2GB)


### Usage
Use [UEFITool (non NE)](https://github.com/LongSoft/UEFITool/releases/tag/0.28.0) to insert the FFS from [Releases](https://github.com/xCuri0/ReBarUEFI/releases) into the DXE driver section and flash the modified firmware.

For more information on inserting FFS DXE modules you can check the guide for inserting NVMe modules on [win-raid forum](https://winraid.level1techs.com/t/howto-get-full-nvme-support-for-all-systems-with-an-ami-uefi-bios/30901).


Once running the modified firmware and 4G Decoding is enabled run ReBarState (found in Releases) and set the Resizable BAR size.

### Build
Use the provided buildffs.py script after cloning inside an edk2 tree to build the DXE driver. ReBarState can be built on Windows or Linux using CMake.
