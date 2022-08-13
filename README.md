# ReBarUEFI
DXE driver to enable resizable BAR on systems which don't support it officially.

### Requirements
* PCIe 3.0 (Ivy Bridge+)
* 4G Decoding enabled
* Suffecient MMIOH space for GPU BAR

Use UEFITool to insert the module before PciHostBridge. Configure by setting ReBarState variable.

### Build
Use the provided buildffs.py script after cloning inside an edk2 tree
