#include <iostream>
#include <Windows.h>
#include <string>

bool notExist = false;

#ifdef _MSC_VER

#define VARIABLE_ATTRIBUTE_NON_VOLATILE 0x00000001
#define VARIABLE_ATTRIBUTE_BOOTSERVICE_ACCESS 0x00000002
#define VARIABLE_ATTRIBUTE_RUNTIME_ACCESS 0x00000004

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
#endif


int main()
{
	std::string i;
	uint8_t reBarState;

	if (!CheckPriviledge()) {
		std::cout << "Failed to obtain EFI variable access try running as admin/root\n";
		std::cin.get();
		return 1;
	}

	reBarState = GetState();

	if (!notExist) {
		std::cout << "Current ReBarState " << +reBarState << "\n";
	}

	std::cout << "\nVerify that 4G Decoding is enabled otherwise system will not POST with GPU. There is also a possibility of BIOS not supporting large BARs even with 4G decoding enabled.\n";
	std::cout << "\nIt is recommended to first try smaller sizes above 256MB in case BIOS doesn't support large BARs.\n";
	std::cout << "\nEnter ReBarState Value\n 0: Disabled \n>0: Maximum BAR size set to 2^x MB \n 32: Unlimited BAR size\n\n";

	std::getline(std::cin, i);

	reBarState = std::stoi(i);
	std::cout << "Writing value of " << pow(2, reBarState) << "MB to ReBarState \n\n";

	if (WriteState(reBarState)) {
		std::cout << "Successfully wrote ReBarState UEFI variable\n";
	}
	else {
		std::cout << "Failed to write ReBarState UEFI variable\n";
	}

	std::cout << "You can close the app now\n";
	std::cin.get();
}