/* 
Gigabyte B75M-D3H F16 BIOS module for ReBarUEFI
*/
#include "../../../include/board.h"
#define MAX_SETUP_SIZE 1239

// EC87D643-EBA4-4BB5-A1E5-3F3E36B20DA9
GUID setupVariableGuid = {0xEC87D643, 0xEBA4, 0x4BB5, {0xA1, 0xE5, 0x3F, 0x3E, 0x36, 0xB2, 0x0D, 0xA9}};

BOOLEAN setupRead = FALSE;
UINT8 *setup;

void readSetup() {
    UINTN bufferSize;
    UINT32 attributes = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;

    setup = AllocateZeroPool(MAX_SETUP_SIZE);
    if (setup) {
        gRT->GetVariable(L"Setup", &setupVariableGuid, 
        &attributes,
        &bufferSize, setup);

        setupRead = TRUE;
    }
}

// Check is 4G decoding is enabled
BOOLEAN mmio4GDecodingEnabled() {
    if (!setupRead)
        readSetup();

    if (setupRead)
        return setup[0x3] != 0;
    else
        return 0;
} 

// Reads ReBar state
BOOLEAN reBarEnabled() {
    if (!setupRead)
        readSetup();   

    if (setupRead)
        return setup[0x494] != 0;
    else
        return 0;
}

void boardFree() {
    FreePool(setup);
}