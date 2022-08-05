#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Protocol/PciIo.h>
#include <Protocol/PciRootBridgeIo.h>
#include <IndustryStandard/Pci22.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include "include/pciRegs.h"
#include "include/board.h"

#define CAP_POINTER 0x34
#define PCIE_DEVICE 0x10

#define PCI_POSSIBLE_ERROR(val) ((val) == 0xffffffff)

// for quirk
#define PCI_VENDOR_ID_ATI 0x1002

EFI_HANDLE iHandle;
EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *pciRootBridgeIo;

INTN fls(UINTN x)
{
    INTN r = 32;

    if (!x)
        return 0;
    if (!(x & 0xffff0000u))
    {
        x <<= 16;
        r -= 16;
    }
    if (!(x & 0xff000000u))
    {
        x <<= 8;
        r -= 8;
    }
    if (!(x & 0xf0000000u))
    {
        x <<= 4;
        r -= 4;
    }
    if (!(x & 0xc0000000u))
    {
        x <<= 2;
        r -= 2;
    }
    if (!(x & 0x80000000u))
    {
        x <<= 1;
        r -= 1;
    }
    return r;
}

UINT64 pciAddrOffset(UINTN pciAddress, UINTN offset)
{
    UINTN reg = (pciAddress & 0xffffffff00000000) >> 32;
    UINTN bus = (pciAddress & 0xff000000) >> 24;
    UINTN dev = (pciAddress & 0xff0000) >> 16;
    UINTN func = (pciAddress & 0xff00) >> 8;

    return EFI_PCI_ADDRESS(bus, dev, func, reg + offset);
}

// created these functions to make it easy to read as we are adapting alot of code from Linux
EFI_STATUS pciReadConfigDword(UINTN pciAddress, INTN pos, UINT32 *buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint32, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciWriteConfigDword(UINTN pciAddress, INTN pos, UINT32 *buf)
{
    return pciRootBridgeIo->Pci.Write(pciRootBridgeIo, EfiPciWidthUint32, pciAddrOffset(pciAddress, pos), 1, buf);
}
EFI_STATUS pciReadConfigWord(UINTN pciAddress, INTN pos, UINT16 *buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint16, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciWriteConfigWord(UINTN pciAddress, INTN pos, UINT16 *buf)
{
    return pciRootBridgeIo->Pci.Write(pciRootBridgeIo, EfiPciWidthUint16, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciReadConfigByte(UINTN pciAddress, INTN pos, UINT8 *buf)
{
    return pciRootBridgeIo->Pci.Read(pciRootBridgeIo, EfiPciWidthUint8, pciAddrOffset(pciAddress, pos), 1, buf);
}

EFI_STATUS pciWriteConfigByte(UINTN pciAddress, INTN pos, UINT8 *buf)
{
    return pciRootBridgeIo->Pci.Write(pciRootBridgeIo, EfiPciWidthUint8, pciAddrOffset(pciAddress, pos), 1, buf);
}

BOOLEAN isPCIeDevice(UINTN pciAddress)
{
    UINT8 buf = 0xff;
    UINT16 offset = CAP_POINTER;
    UINTN tempPciAddress;

    pciReadConfigByte(pciAddress, offset, &buf);
    offset = buf;
    while (buf)
    {
        tempPciAddress = offset;
        pciReadConfigByte(pciAddress, tempPciAddress, &buf);
        if (buf != PCIE_DEVICE)
        {
            tempPciAddress += 1;
            pciReadConfigByte(pciAddress, tempPciAddress, &buf);
            offset = buf;
        }
        else
            return TRUE;
    }
    return FALSE;
}

// adapted from linux pci_find_ext_capability
UINT16 pciFindExtCapability(UINTN pciAddress, INTN cap)
{
    INTN ttl;
    UINT32 header;
    UINT16 pos = PCI_CFG_SPACE_SIZE;

    /* minimum 8 bytes per capability */
    ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

    /* Only PCIe devices support extended configuration space */
    if (!isPCIeDevice(pciAddress))
        return 0;

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

INTN pciRebarFindPos(UINTN pciAddress, UINTN pos, UINT8 bar)
{
    UINTN nbars, i;
    UINT32 ctrl;

    pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
    nbars = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >>
            PCI_REBAR_CTRL_NBAR_SHIFT;

    for (i = 0; i < nbars; i++, pos += 8)
    {
        UINTN bar_idx;

        pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
        bar_idx = ctrl & PCI_REBAR_CTRL_BAR_IDX;
        if (bar_idx == bar)
            return pos;
    }
    return -1;
}

UINT8 pciRebarGetCurrentSize(UINTN pciAddress, UINTN epos, UINT8 bar)
{
    INTN pos;
    UINT32 ctrl;

    pos = pciRebarFindPos(pciAddress, epos, bar);
    if (pos < 0)
        return 0;

    pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
    return (ctrl & PCI_REBAR_CTRL_BAR_SIZE) >> PCI_REBAR_CTRL_BAR_SHIFT;
}

UINT32 pciRebarGetPossibleSizes(UINTN pciAddress, UINTN epos, UINT16 vid, UINT16 did, UINT8 bar)
{
    INTN pos;
    UINT32 cap;

    pos = pciRebarFindPos(pciAddress, epos, bar);
    if (pos < 0)
        return 0;

    pciReadConfigDword(pciAddress, pos + PCI_REBAR_CAP, &cap);
    cap &= PCI_REBAR_CAP_SIZES;

    /* Sapphire RX 5600 XT Pulse has an invalid cap dword for BAR 0 */
    if (vid == PCI_VENDOR_ID_ATI && did == 0x731f &&
        bar == 0 && cap == 0x7000)
        cap = 0x3f000;

    return cap >> 4;
}

INTN pciRebarSetSize(UINTN pciAddress, UINTN epos, UINT8 bar, UINT8 size)
{
    INTN pos;
    UINT32 ctrl;

    pos = pciRebarFindPos(pciAddress, epos, bar);
    if (pos < 0)
        return pos;

    pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
    ctrl &= ~PCI_REBAR_CTRL_BAR_SIZE;
    ctrl |= size << PCI_REBAR_CTRL_BAR_SHIFT;

    pciWriteConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
    return 0;
}

VOID scanPCIDevices(UINT16 maxBus)
{
    UINT8 hdr_type;
    UINTN bus, fun, dev;
    UINTN pciAddress;
    UINT16 vid, did;
    UINTN epos;
    #ifdef DXE
    UINT16 cmd, val = 0;
    #endif

    for (bus = 0; bus <= maxBus; bus++)
        for (dev = 0; dev <= PCI_MAX_DEVICE; dev++)
            for (fun = 0; fun <= PCI_MAX_FUNC; fun++)
            {
                pciAddress = EFI_PCI_ADDRESS(bus, dev, fun, 0);
                pciReadConfigWord(pciAddress, 0, &vid);
                pciReadConfigWord(pciAddress, 2, &did);

                if (vid == 0xFFFF)
                    continue;

                epos = pciFindExtCapability(pciAddress, PCI_EXT_CAP_ID_REBAR);
                if (epos)
                {
                    for (UINT8 bar = 0; bar < 6; bar++)
                    {
                        UINT8 rBarC = pciRebarGetCurrentSize(pciAddress, epos, bar);
                        UINT32 rBarS = pciRebarGetPossibleSizes(pciAddress, epos, vid, did, bar);
                        if (rBarS)
                        {
                            UINT8 maxSize = (UINT8)fls(rBarS) - 1;
                            #ifdef DXE
                            if (maxSize > rBarC)
                            {
                                // not sure if we even need to disable decoding before the resources are allocated
                                if (!cmd)
                                {
                                    pciReadConfigWord(pciAddress, PCI_COMMAND, &cmd);
                                    val = cmd & ~PCI_COMMAND_MEMORY;
                                    pciWriteConfigWord(pciAddress, PCI_COMMAND, &val);
                                }
                                pciRebarSetSize(pciAddress, epos, bar, maxSize);
                            }
                            #else
                            Print(L"BAR %u RBAR CURRENT %u RBAR MAX %u\n", bar, rBarC, maxSize);
                            #endif
                        }
                    }
                }

                #ifdef DXE
                if (cmd)
                {
                    pciWriteConfigWord(pciAddress, PCI_COMMAND, &cmd);
                    cmd = 0;
                }
                #endif

                pciReadConfigByte(pciAddress, PCI_HEADER_TYPE, &hdr_type);
                if (!fun && ((hdr_type & HEADER_TYPE_MULTI_FUNCTION) == 0))
                    break; // no
            }
}

EFI_STATUS
PciGetNextBusRange(
    IN OUT EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR **Descriptors,
    OUT UINT16 *MinBus,
    OUT UINT16 *MaxBus,
    OUT BOOLEAN *IsEnd)
{
    *IsEnd = FALSE;

    //
    // When *Descriptors is NULL, Configuration() is not implemented, so assume
    // range is 0~PCI_MAX_BUS
    //
    if ((*Descriptors) == NULL)
    {
        *MinBus = 0;
        *MaxBus = PCI_MAX_BUS;
        return EFI_SUCCESS;
    }

    //
    // *Descriptors points to one or more address space descriptors, which
    // ends with a end tagged descriptor. Examine each of the descriptors,
    // if a bus typed one is found and its bus range covers bus, this handle
    // is the handle we are looking for.
    //

    while ((*Descriptors)->Desc != ACPI_END_TAG_DESCRIPTOR)
    {
        if ((*Descriptors)->ResType == ACPI_ADDRESS_SPACE_TYPE_BUS)
        {
            *MinBus = (UINT16)(*Descriptors)->AddrRangeMin;
            *MaxBus = (UINT16)(*Descriptors)->AddrRangeMax;
            (*Descriptors)++;
            return (EFI_SUCCESS);
        }

        (*Descriptors)++;
    }

    if ((*Descriptors)->Desc == ACPI_END_TAG_DESCRIPTOR)
    {
        *IsEnd = TRUE;
    }

    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI uefiMain(
    IN EFI_HANDLE imageHandle,
    IN EFI_SYSTEM_TABLE *systemTable)
{

    EFI_STATUS status;
    UINTN handleCount;
    EFI_HANDLE *handleBuffer;
    UINTN i;
    EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *descriptors;
    UINT16 minBus, maxBus;
    BOOLEAN isEnd;
    BOOLEAN mmio4GDecodeEnable, reBarEnable;

    mmio4GDecodeEnable = mmio4GDecodingEnabled();
    reBarEnable = reBarEnabled();

    #ifndef DXE
    Print(L"4G Decoding: %u\nResizable BAR: %u\n", mmio4GDecodeEnable, reBarEnable);
    #endif

    // If 4G decoding is off PciHostBridge will fail to allocate resources
    if (mmio4GDecodeEnable && reBarEnable)
    {
        iHandle = imageHandle;
        status = gBS->LocateHandleBuffer(
            ByProtocol,
            &gEfiPciRootBridgeIoProtocolGuid,
            NULL,
            &handleCount,
            &handleBuffer);
        ASSERT_EFI_ERROR(status);
        for (i = 0; i < handleCount; i++)
        {
            status = gBS->HandleProtocol(handleBuffer[i], &gEfiPciRootBridgeIoProtocolGuid, (void **)&pciRootBridgeIo);
            ASSERT_EFI_ERROR(status);
            status = pciRootBridgeIo->Configuration(pciRootBridgeIo, (VOID **)&descriptors);
            ASSERT_EFI_ERROR(status);

            while (TRUE)
            {
                status = PciGetNextBusRange(&descriptors, &minBus, &maxBus, &isEnd);
                ASSERT_EFI_ERROR(status);

                if (isEnd || descriptors == NULL)
                    break;
                scanPCIDevices(maxBus);
            }
        }
        FreePool(handleBuffer);
    }
    boardFree();

    return EFI_SUCCESS;
}
