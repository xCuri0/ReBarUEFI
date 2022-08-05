#include <Uefi.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>

// Check is 4G decoding is enabled
BOOLEAN mmio4GDecodingEnabled();

// Reads ReBar state
BOOLEAN reBarEnabled();

// Free
void boardFree();
