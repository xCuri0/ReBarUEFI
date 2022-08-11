[DEFINES]
	DSC_SPECIFICATION = 1.28
	PLATFORM_NAME = ReBarUEFI
	PLATFORM_GUID = a1c8d6a2-baf6-4959-8989-66e175651440
	PLATFORM_VERSION = 1.00
	OUTPUT_DIRECTORY = Build/ReBarUEFI
	SUPPORTED_ARCHITECTURES = X64
	BUILD_TARGETS = DEBUG|RELEASE|NOOPT
	SKUID_IDENTIFIER = DEFAULT
	
[Components]
	ReBarUEFI/ReBarDxe/ReBarTest.inf
	ReBarUEFI/ReBarDxe/ReBarDxe.inf
	
!include MdePkg/MdeLibs.dsc.inc
[LibraryClasses]
	UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
	UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
	UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
	UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
	UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
	PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
	DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
	BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
	PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
	BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
	ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
	MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
	DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
	FileHandleLib|MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf
	HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
	SortLib|MdeModulePkg/Library/BaseSortLib/BaseSortLib.inf
	UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf