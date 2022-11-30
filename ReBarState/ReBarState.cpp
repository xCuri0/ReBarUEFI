/*
Copyright (c) 2022 xCuri0 <zkqri0@gmail.com>
SPDX-License-Identifier: MIT
*/
#include <iostream>
#include <string>
#include <cmath>

#ifdef _MSC_VER
#include <Windows.h>
#else
#include <unistd.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif

#define QUOTE(x) #x
#define STR(x) QUOTE(x)

#define VNAME ReBarState
#define VGUID A3C5B77A-C88F-4A93-BF1C-4A92A32C65CE

#define VARIABLE_ATTRIBUTE_NON_VOLATILE 0x00000001
#define VARIABLE_ATTRIBUTE_BOOTSERVICE_ACCESS 0x00000002
#define VARIABLE_ATTRIBUTE_RUNTIME_ACCESS 0x00000004

bool notExist = false;

// Windows
#ifdef _MSC_VER
bool CheckPriviledge()
{
	DWORD len;
	HANDLE hTok;
	TOKEN_PRIVILEGES tokp;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
		&hTok)) {
		return FALSE;
	}

	LookupPrivilegeValue(NULL, SE_SYSTEM_ENVIRONMENT_NAME, &tokp.Privileges[0].Luid);
	tokp.PrivilegeCount = 1;
	tokp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	AdjustTokenPrivileges(hTok, FALSE, &tokp, 0, NULL, &len);

	if (GetLastError() != ERROR_SUCCESS) {
		std::cout << "Failed to obtain SE_SYSTEM_ENVIRONMENT_NAME\n";
		return FALSE;
	}
	std::cout << "Obtained SE_SYSTEM_ENVIRONMENT_NAME\n";
	return TRUE;
}

uint8_t GetState() {
	UINT8 rBarState;
	DWORD rSize;

	const TCHAR name[] = TEXT("ReBarState");
	const TCHAR guid[] = TEXT("{A3C5B77A-C88F-4A93-BF1C-4A92A32C65CE}");

	rSize = GetFirmwareEnvironmentVariable(name, guid, &rBarState, 1);

	if (rSize == 1)
		return rBarState;
	else {
		notExist = true;
		return 0;
	}
}

bool WriteState(uint8_t rBarState) {
	DWORD size = sizeof(UINT8);
	DWORD dwAttributes = VARIABLE_ATTRIBUTE_NON_VOLATILE | VARIABLE_ATTRIBUTE_BOOTSERVICE_ACCESS | VARIABLE_ATTRIBUTE_RUNTIME_ACCESS;

	const TCHAR name[] = TEXT("ReBarState");
	const TCHAR guid[] = TEXT("{A3C5B77A-C88F-4A93-BF1C-4A92A32C65CE}");

	return SetFirmwareEnvironmentVariableEx(name, guid, &rBarState, size, dwAttributes) != 0;
}
// Linux
#else

#define REBARPATH /sys/firmware/efi/efivars/VNAME-VGUID
#define REBARPS STR(REBARPATH)

struct __attribute__((__packed__)) rebarVar {
	uint32_t attr;
	uint8_t value;
};

bool CheckPriviledge() {
	return getuid() == 0;
}

uint8_t GetState() {
	uint8_t rebarState[5];

	FILE* f = fopen(REBARPS, "rb");

	if (!(f && (fread(&rebarState, 5, 1, f) == 1))) {
		rebarState[4] = 0;
		notExist = true;
	} else
		fclose(f);

	return rebarState[4];
}

bool WriteState(uint8_t rBarState) {
	int attr;
	rebarVar rVar;
	FILE* f = fopen(REBARPS, "rb");

	if (f) {
		// remove immuteable flag that linux sets on all unknown efi variables
		ioctl(fileno(f), FS_IOC_GETFLAGS, &attr);
		attr &= ~FS_IMMUTABLE_FL;
		ioctl(fileno(f), FS_IOC_SETFLAGS, &attr);

		if (remove(REBARPS) != 0) {
			std::cout << "Failed to remove old variable\n";
			return false;
		}
	}

	f = fopen(REBARPS, "wb");

	rVar.attr = VARIABLE_ATTRIBUTE_NON_VOLATILE | VARIABLE_ATTRIBUTE_BOOTSERVICE_ACCESS | VARIABLE_ATTRIBUTE_RUNTIME_ACCESS;
	rVar.value = rBarState;	

	return fwrite(&rVar, sizeof(rVar), 1, f) == 1;;
}
#endif


int main()
{
	int ret = 0;
	std::string i;	
	uint8_t reBarState;

	std::cout << "ReBarState (c) 2022 xCuri0\n\n";
	if (!CheckPriviledge()) {
		std::cout << "Failed to obtain EFI variable access try running as admin/root\n";
		ret = 1;
		goto exit;
	}

	reBarState = GetState();

	if (!notExist) {
		if (reBarState == 0)
			std::cout << "Current ReBarState " << +reBarState << " / Disabled\n";
		else
			if (reBarState == 32)
				std::cout << "Current ReBarState " << +reBarState << " / Unlimited\n";
			else
				std::cout << "Current ReBarState " << +reBarState << " / " << std::pow(2, reBarState) << " MB\n";
	}
	else {
		std::cout << "ReBarState variable doesn't exist / Disabled. Enter a value to create it\n";
	}

	std::cout << "\nVerify that 4G Decoding is enabled otherwise system will not POST with GPU. There is also a possibility of BIOS not supporting large BARs even with 4G decoding enabled.\n";
	std::cout << "\nIt is recommended to first try smaller sizes above 256MB in case BIOS doesn't support large BARs.\n";
	std::cout << "\nEnter ReBarState Value\n      0: Disabled \nAbove 0: Maximum BAR size set to 2^x MB \n     32: Unlimited BAR size\n\n";

	std::getline(std::cin, i);

	if (std::stoi(i) > 32) {
		std::cout << "Invalid value\n";
		goto exit;
	}
	reBarState = std::stoi(i);

	if (reBarState < 20)
		if (reBarState == 0)
			std::cout << "Writing value of 0 / Disabled to ReBarState\n\n";
		else
			std::cout << "Writing value of " << +reBarState << " / " << std::pow(2, reBarState) << " MB to ReBarState\n\n";
	else
		std::cout << "Writing value to ReBarState\n\n";

	if (WriteState(reBarState)) {
		std::cout << "Successfully wrote ReBarState UEFI variable\n";
		std::cout << "\nReboot for changes to take effect\n";
	}
	else {
		std::cout << "Failed to write ReBarState UEFI variable\n";
		#ifdef _MSC_VER
		std::cout << "GetLastError: " << GetLastError() << "\n";
		#endif
	}

	// Linux will probably be run from terminal not requiring this
	#ifdef _MSC_VER
	std::cout << "You can close the app now\n";
exit:
	std::cin.get();
	#else
exit:
	#endif
	return ret;
}