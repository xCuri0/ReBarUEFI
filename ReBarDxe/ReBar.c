/*
Copyright (c) 2022-2023 xCuri0 <zkqri0@gmail.com>
SPDX-License-Identifier: MIT
*/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/PciRootBridgeIo.h>
#include <IndustryStandard/Pci22.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include "include/pciRegs.h"
#include "include/PciHostBridgeResourceAllocation.h"

#define PCI_POSSIBLE_ERROR(val) ((val) == 0xffffffff)

// for quirk
#define PCI_VENDOR_ID_ATI 0x1002

// if system time is before the build year then CMOS reset will be detected and rebar will be disabled.
// year is parsed from __DATE__ ("Mmm dd yyyy") so it can't go stale.
#define BUILD_YEAR (((__DATE__[7] - '0') * 1000) + ((__DATE__[8] - '0') * 100) + \
                    ((__DATE__[9] - '0') * 10) + (__DATE__[10] - '0'))

// maximum number of PCI host bridges we can hook
#define MAX_HOST_BRIDGES 8

// a3c5b77a-c88f-4a93-bf1c-4a92a32c65ce
static GUID reBarStateGuid = { 0xa3c5b77a, 0xc88f, 0x4a93, {0xbf, 0x1c, 0x4a, 0x92, 0xa3, 0x2c, 0x65, 0xce}};

// 0: disabled
// >0: maximum BAR size set to 2^x MB. 32 (or higher) for unlimited
static UINT8 reBarState = 0;

static EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *pciRootBridgeIo;

// hooked host bridge protocols and their original PreprocessController functions
typedef struct {
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL                       *protocol;
    EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL_PREPROCESS_CONTROLLER original;
} HOST_BRIDGE_HOOK;

static HOST_BRIDGE_HOOK hostBridgeHooks[MAX_HOST_BRIDGES];
static UINTN hostBridgeHookCount = 0;

static UINT64 pciAddrOffset(UINTN pciAddress, INTN offset)
{
    UINTN reg = (pciAddress & 0xffffffff00000000) >> 32;
    UINTN bus = (pciAddress & 0xff000000) >> 24;
    UINTN dev = (pciAddress & 0xff0000) >> 16;
    UINTN func = (pciAddress & 0xff00) >> 8;

    return EFI_PCI_ADDRESS(bus, dev, func, ((INT64)reg + offset));
}

// created these functions to make it easy to read as we are adapting alot of code from Linux
static EFI_STATUS pciReadConfigDword(UINTN pciAddress, INTN pos, UINT32 *buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint32, pciAddrOffset(pciAddress, pos), 1, buf);
}

static EFI_STATUS pciWriteConfigDword(UINTN pciAddress, INTN pos, UINT32 *buf)
{
    return pciRootBridgeIo->Pci.Write(pciRootBridgeIo, EfiPciWidthUint32, pciAddrOffset(pciAddress, pos), 1, buf);
}

static EFI_STATUS pciReadConfigWord(UINTN pciAddress, INTN pos, UINT16 *buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint16, pciAddrOffset(pciAddress, pos), 1, buf);
}

// adapted from linux pci_find_ext_capability
static UINT16 pciFindExtCapability(UINTN pciAddress, INTN cap)
{
    INTN ttl;
    UINT32 header;
    UINT16 pos = PCI_CFG_SPACE_SIZE;

    /* minimum 8 bytes per capability */
    ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos, &header)))
        return 0;
    /*
     * If we have no capabilities, this is indicated by cap ID,
     * cap version and next pointer all being 0. Or it could also be all FF
     */
    if (header == 0 || PCI_POSSIBLE_ERROR(header))
        return 0;

    while (ttl-- > 0)
    {
        if (PCI_EXT_CAP_ID(header) == cap && pos != 0)
            return pos;

        pos = PCI_EXT_CAP_NEXT(header);
        if (pos < PCI_CFG_SPACE_SIZE)
            break;

        if (EFI_ERROR(pciReadConfigDword(pciAddress, pos, &header)))
            break;
    }
    return 0;
}

static INTN pciRebarFindPos(UINTN pciAddress, INTN pos, UINT8 bar)
{
    UINTN nbars, i;
    UINT32 ctrl;

    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl)))
        return -1;

    nbars = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >>
            PCI_REBAR_CTRL_NBAR_SHIFT;

    for (i = 0; i < nbars; i++, pos += 8)
    {
        UINTN bar_idx;

        if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl)))
            return -1;

        bar_idx = ctrl & PCI_REBAR_CTRL_BAR_IDX;
        if (bar_idx == bar)
            return pos;
    }
    return -1;
}

static UINT32 pciRebarGetPossibleSizes(UINTN pciAddress, UINTN epos, UINT16 vid, UINT16 did, UINT8 bar)
{
    INTN pos;
    UINT32 cap;

    pos = pciRebarFindPos(pciAddress, (INTN)epos, bar);
    if (pos < 0)
        return 0;

    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CAP, &cap)))
        return 0;

    cap &= PCI_REBAR_CAP_SIZES;

    /* Sapphire RX 5600 XT Pulse has an invalid cap dword for BAR 0 */
    if (vid == PCI_VENDOR_ID_ATI && did == 0x731f &&
        bar == 0 && cap == 0x7000)
        cap = 0x3f000;

    return cap >> 4;
}

static INTN pciRebarSetSize(UINTN pciAddress, UINTN epos, UINT8 bar, UINT8 size)
{
    INTN pos;
    UINT32 ctrl;

    pos = pciRebarFindPos(pciAddress, (INTN)epos, bar);
    if (pos < 0)
        return pos;

    if (EFI_ERROR(pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl)))
        return -1;

    ctrl &= (UINT32)~PCI_REBAR_CTRL_BAR_SIZE;
    ctrl |= (UINT32)size << PCI_REBAR_CTRL_BAR_SHIFT;

    if (EFI_ERROR(pciWriteConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl)))
        return -1;

    return 0;
}

static VOID reBarSetupDevice(EFI_HANDLE handle, EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ADDRESS addrInfo)
{
    UINTN epos;
    UINT16 vid, did;
    UINTN pciAddress;
    EFI_STATUS status;

    status = gBS->HandleProtocol(handle, &gEfiPciRootBridgeIoProtocolGuid, (VOID **)&pciRootBridgeIo);
    if (EFI_ERROR(status))
        return;

    pciAddress = EFI_PCI_ADDRESS(addrInfo.Bus, addrInfo.Device, addrInfo.Function, 0);

    if (EFI_ERROR(pciReadConfigWord(pciAddress, 0, &vid)))
        return;
    if (EFI_ERROR(pciReadConfigWord(pciAddress, 2, &did)))
        return;

    if (vid == 0xFFFF)
        return;

    DEBUG((DEBUG_INFO, "ReBarDXE: Device vid:%x did:%x\n", vid, did));

    epos = pciFindExtCapability(pciAddress, PCI_EXT_CAP_ID_REBAR);
    if (epos)
    {
        for (UINT8 bar = 0; bar < 6; bar++)
        {
            UINT32 rBarS = pciRebarGetPossibleSizes(pciAddress, epos, vid, did, bar);
            if (!rBarS)
                continue;
            // start with the highest supported size
            for (UINT8 n = MIN((UINT8)HighBitSet32(rBarS), reBarState); n > 0; n--) {
                // check if size is supported
                if (rBarS & (1U << n)) {
                    pciRebarSetSize(pciAddress, epos, bar, n);
                    break;
                }
            }
        }
    }
}

static EFI_STATUS EFIAPI PreprocessControllerOverride (
  IN  EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL  *This,
  IN  EFI_HANDLE                                        RootBridgeHandle,
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_PCI_ADDRESS       PciAddress,
  IN  EFI_PCI_CONTROLLER_RESOURCE_ALLOCATION_PHASE      Phase
  )
{
    EFI_STATUS status = EFI_SUCCESS;

    // call the original method of the host bridge being preprocessed
    for (UINTN i = 0; i < hostBridgeHookCount; i++) {
        if (hostBridgeHooks[i].protocol == This) {
            status = hostBridgeHooks[i].original(This, RootBridgeHandle, PciAddress, Phase);
            break;
        }
    }

    DEBUG((DEBUG_INFO, "ReBarDXE: Hooked PreprocessController called %d\n", Phase));

    // EDK2 PciBusDxe setups Resizable BAR twice so we will do same
    if (Phase <= EfiPciBeforeResourceCollection) {
        reBarSetupDevice(RootBridgeHandle, PciAddress);
    }

    return status;
}

static VOID pciHostBridgeResourceAllocationProtocolHook(VOID)
{
    EFI_STATUS status;
    UINTN handleCount = 0;
    EFI_HANDLE *handleBuffer = NULL;

    status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiPciHostBridgeResourceAllocationProtocolGuid,
        NULL,
        &handleCount,
        &handleBuffer);

    if (EFI_ERROR(status))
        goto free;

    for (UINTN i = 0; i < handleCount && hostBridgeHookCount < MAX_HOST_BRIDGES; i++)
    {
        EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL *pciResAlloc;

        status = gBS->OpenProtocol(
            handleBuffer[i],
            &gEfiPciHostBridgeResourceAllocationProtocolGuid,
            (VOID **)&pciResAlloc,
            gImageHandle,
            NULL,
            EFI_OPEN_PROTOCOL_GET_PROTOCOL);

        if (EFI_ERROR(status))
            continue;

        DEBUG((DEBUG_INFO, "ReBarDXE: Hooking EfiPciHostBridgeResourceAllocationProtocol->PreprocessController\n"));

        // Hook PreprocessController
        hostBridgeHooks[hostBridgeHookCount].protocol = pciResAlloc;
        hostBridgeHooks[hostBridgeHookCount].original = pciResAlloc->PreprocessController;
        hostBridgeHookCount++;
        pciResAlloc->PreprocessController = &PreprocessControllerOverride;
    }

free:
    if (handleBuffer != NULL)
        FreePool(handleBuffer);
}

EFI_STATUS EFIAPI rebarInit(
    IN EFI_HANDLE imageHandle,
    IN EFI_SYSTEM_TABLE *systemTable)
{
    UINTN bufferSize = 1;
    EFI_STATUS status;
    UINT32 attributes;
    EFI_TIME time;

    DEBUG((DEBUG_INFO, "ReBarDXE: Loaded\n"));

    // Read ReBarState variable
    status = gRT->GetVariable(L"ReBarState", &reBarStateGuid,
        &attributes,
        &bufferSize, &reBarState);

    // any attempts to overflow reBarState should result in EFI_BUFFER_TOO_SMALL
    if (status != EFI_SUCCESS)
        reBarState = 0;

    if (reBarState)
    {
        DEBUG((DEBUG_INFO, "ReBarDXE: Enabled, maximum BAR size 2^%u MB\n", reBarState));

        // Detect CMOS reset by checking if year before BUILD_YEAR
        status = gRT->GetTime (&time, NULL);
        if (!EFI_ERROR(status) && time.Year < BUILD_YEAR) {
            reBarState = 0;
            bufferSize = 1;
            attributes = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;

            gRT->SetVariable(L"ReBarState", &reBarStateGuid,
                attributes,
                bufferSize, &reBarState);

            return EFI_SUCCESS;
        }

        // For overriding PciHostBridgeResourceAllocationProtocol
        pciHostBridgeResourceAllocationProtocolHook();
    }

    return EFI_SUCCESS;
}
