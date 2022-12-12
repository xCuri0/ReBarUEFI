# ReBarUEFI
DXE driver to enable Resizable BAR on systems which don't support it officially. This is done by hooking ```PreprocessController``` which is called for every PCI device during boot and setting up the Resizable BAR control registers.

![screenshot showing cpu-z, gpu-z and amd software](rebar.png)

[![ReBarDxe EDK2](https://github.com/xCuri0/ReBarUEFI/actions/workflows/ReBarDxe.yml/badge.svg)](https://github.com/xCuri0/ReBarUEFI/actions/workflows/ReBarDxe.yml)
[![ReBarState CMake](https://github.com/xCuri0/ReBarUEFI/actions/workflows/ReBarState.yml/badge.svg)](https://github.com/xCuri0/ReBarUEFI/actions/workflows/ReBarState.yml)

### Requirements
* 4G Decoding enabled. See wiki page [Enabling hidden 4G decoding](https://github.com/xCuri0/ReBarUEFI/wiki/Enabling-hidden-4G-decoding) if you can't find an option for it.
* (optional) BIOS support for Large BARs

### Usage
Use [UEFITool (non NE)](https://github.com/LongSoft/UEFITool/releases/tag/0.28.0) to insert the FFS from [Releases](https://github.com/xCuri0/ReBarUEFI/releases) into the end of the DXE driver section and flash the modified firmware.

See wiki page [Adding FFS module](https://github.com/xCuri0/ReBarUEFI/wiki/Adding-FFS-module) for more information.


Once running the modified firmware make sure that **4G decoding is enabled and CSM is off**.

Run ReBarState (found in Releases) and set the Resizable BAR size. **If Resizable BAR works for you reply to [List of working motherboards](https://github.com/xCuri0/ReBarUEFI/issues/11) so I can add it to the list.** Most firmware will accept unsigned/patched modules with Secure Boot on so you won't have any problems running certain games.

If you have any issues after enabling Resizable BAR see [Common Issues (and fixes)](https://github.com/xCuri0/ReBarUEFI/wiki/Common-issues-(and-fixes))

**Some firmware don't clear NVRAM variables (ReBarState) when the CMOS is cleared. This can be a problem as CMOS clear will reset BIOS settings (4G/CSM) while keeping ReBarState enabled requiring you to boot with iGPU/non-rebar GPU to disable ReBarState. To mitigate this issue you can use AMIBCP to enable 4G decode and disable CSM by default. If it can be figured out how to detect boot failures then this issue can be solved**

### X99 Tutorial by Miyconst
[![Resizable BAR on LGA 2011-3 X99](http://img.youtube.com/vi/vcJDWMpxpjE/0.jpg)](http://www.youtube.com/watch?v=vcJDWMpxpjEE "Resizable BAR on LGA 2011-3 X99")

Instructions for applying UEFIPatch not included as it isn't required for these X99 motherboards. You can follow them below.

### UEFI Patching
Most UEFI firmwares have problems handling 64-bit BARs so several patches were created to fix these issues. You can use [UEFIPatch](https://github.com/LongSoft/UEFITool/releases/tag/0.28.0) to apply these patches located in the UEFIPatch folder. Some patches which may cause issues are commented and need to be manually uncommented. See wiki page [Using UEFIPatch](https://github.com/xCuri0/ReBarUEFI/wiki/Using-UEFIPatch) for more information on using UEFIPatch. **Make sure to check that pad files aren't changed and if they are use the workaround**

#### Working patches
* <4GB BAR size limit removal
* <16GB BAR size limit removal
* <64GB BAR size limit removal
* Prevent 64-bit BARs from being downgraded to 32-bit
* Increase MMIO space to 64GB (Haswell/Broadwell). Full 512GB/39-bit isn't possible yet.
* Increase MMIO space from 16GB to full usage of 64GB/36-bit range (Sandy/Ivy Bridge). **Requires DSDT modification on certain motherboards (mostly Gigabyte) so commented (#) by default. See wiki page [DSDT Patching](https://github.com/xCuri0/ReBarUEFI/wiki/DSDT-Patching) for more information.**
* Remove NVRAM whitelist to solve ReBarState ```GetLastError: 5```

### Build
Use the provided buildffs.py script after cloning inside an edk2 tree to build the DXE driver. ReBarState can be built on Windows or Linux using CMake. See wiki page [Building](https://github.com/xCuri0/ReBarUEFI/wiki/Building) for more information.

### FAQ
#### Why don't BAR sizes above x size work ?
If you can't use 4GB and larger sizes it means your BIOS doesn't support large BARs. If you can't use above 1GB then either 4G decoding is disabled or your BIOS isn't allocating your GPU in the 64-bit region. Patches exist to fix some of these issues.

#### Will less than optimal BAR sizes still give a performance increase ?
On my system with an i5 3470 and Sapphire Nitro+ RX 580 8GB with Nimez drivers/registry edit I get an upto 12% FPS increase with 2GB BAR size.

#### I set an unsupported BAR size and my system won't boot
CMOS reset should fix it but on some motherboards it doesn't which means you will have to either boot with iGPU only (or non rebar GPU) and disable in ReBarState or use BIOS flashback/recovery.

#### Does it work on PCIe Gen2 systems ?
Previously it was thought that it won't work on PCIe 2.0 systems but one user had it work with an i5 2500k.

### Credit
* [@dsanke](https://github.com/dsanke), [@cursemex](https://github.com/cursemex), [@val3nt33n](https://github.com/@val3nt33n), [@Mak3rde](https://github.com/Mak3rde) and [@romulus2k4](https://github.com/romulus2k4) for testing/helping develop patches

* Linux kernel especially the amdgpu driver

* EDK2 for the base that all OEM UEFI follows making hooking easier

* QEMU/OVMF made testing hooking way easier although it doesn't have resizable BAR devices
