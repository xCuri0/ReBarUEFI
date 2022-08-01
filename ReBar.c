#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Protocol/PciIo.h>
#include <Protocol/PciRootBridgeIo.h>
#include <IndustryStandard/Pci22.h>
#include <Library/ShellLib.h>
#include <Library/UefiShellDebug1CommandsLib/Pci.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include "include/pciRegs.h"
#include "include/ioPort.h"
#include "board/board.h"

#define CAP_POINTER 0x34
#define PCIE_DEVICE 0x10
#define PCI_COMMAND_DECODE_ENABLE (PCI_COMMAND_MEMORY | PCI_COMMAND_IO)

// x86 specific
#define IO_SPACE_LIMIT 0xffff

#define PCI_ERROR_RESPONSE		(~0ULL)
#define PCI_POSSIBLE_ERROR(val)		((val) == ((typeof(val)) PCI_ERROR_RESPONSE))
#define upper_32_bits(n) ((UINT32)(((n) >> 16) >> 16))

EFI_HANDLE iHandle;
EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *pciRootBridgeIo;

struct pciMemoryRange {
    UINT64 start;
    UINT64 end;
    UINT64 flags;
	UINT8 resizableSize;
};
struct pciBridgeInfo {
    UINT8 primary;
    UINT8 secondary;
    UINT8 subordinate;
};

struct pciDev {
	EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *pciRootBridgeIo;
    UINT64 pciAddress;
	UINT16 vid;
	UINT16 did;
    struct pciMemoryRange bar[6];
    // linux calls this BAR 6
    struct pciMemoryRange rom;
	struct pciMemoryRange mmioWindow;
	struct pciMemoryRange mmioPrefWindow;
	struct pciBridgeInfo bridgeInfo;
	BOOLEAN isBridge;
    UINT8 romBaseReg;
};

enum pci_bar_type {
	pci_bar_unknown,	/* Standard PCI BAR probe */
	pci_bar_mem32,		/* A 32-bit memory BAR */
	pci_bar_mem64,		/* A 64-bit memory BAR */
};

/* For PCI devices, the region numbers are assigned this way: */
enum {
	/* #0-5: standard PCI resources */
	PCI_STD_RESOURCES,
	PCI_STD_RESOURCE_END = PCI_STD_RESOURCES + PCI_STD_NUM_BARS - 1,
	/* #6: expansion ROM resource */
	PCI_ROM_RESOURCE,
};


INTN fls(UINTN x)
{
	INTN r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
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
    INTN pos = PCI_CFG_SPACE_SIZE;

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

INTN pciRebarFindPos(UINTN pciAddress, UINTN bar)
{
	UINTN pos, nbars, i;
	UINT32 ctrl;

	pos = pciFindExtCapability(pciAddress, PCI_EXT_CAP_ID_REBAR);
	if (!pos)
		return -1;

	pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
	nbars = (ctrl & PCI_REBAR_CTRL_NBAR_MASK) >>
		    PCI_REBAR_CTRL_NBAR_SHIFT;

	for (i = 0; i < nbars; i++, pos += 8) {
		INTN bar_idx;

		pciReadConfigDword(pciAddress,  pos + PCI_REBAR_CTRL, &ctrl);
		bar_idx = ctrl & PCI_REBAR_CTRL_BAR_IDX;
		if (bar_idx == bar)
			return pos;
	}
	return -1;
}

INTN pciRebarGetCurrentSize(UINTN pciAddress, UINTN bar)
{
	INTN pos;
	UINT32 ctrl;

	pos = pciRebarFindPos(pciAddress, bar);
	if (pos < 0)
		return pos;

	pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
	return (ctrl & PCI_REBAR_CTRL_BAR_SIZE) >> PCI_REBAR_CTRL_BAR_SHIFT;
}

#define PCI_VENDOR_ID_ATI		0x1002

UINT32 pciRebarGetPossibleSizes(struct pciDev *pDev, UINTN bar)
{
    INTN pos;
	UINT32 cap;

	pos = pciRebarFindPos(pDev->pciAddress, bar);
	if (pos < 0)
		return 0;

	pciReadConfigDword(pDev->pciAddress, pos + PCI_REBAR_CAP, &cap);
	cap &= PCI_REBAR_CAP_SIZES;

	/* Sapphire RX 5600 XT Pulse has an invalid cap dword for BAR 0 */
	if (pDev->vid == PCI_VENDOR_ID_ATI && pDev->did == 0x731f &&
	    bar == 0 && cap == 0x7000)
		cap = 0x3f000;

	return cap >> 4;
}

INTN pciRebarSetSize(UINTN pciAddress, UINTN bar, UINTN size)
{
	INTN pos;
	UINT32 ctrl;

	pos = pciRebarFindPos(pciAddress, bar);
	if (pos < 0)
		return pos;

	pciReadConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
	ctrl &= ~PCI_REBAR_CTRL_BAR_SIZE;
	ctrl |= size << PCI_REBAR_CTRL_BAR_SHIFT;

    pciWriteConfigDword(pciAddress, pos + PCI_REBAR_CTRL, &ctrl);
	return 0;
}

UINT64 decodeBar(UINT32 bar)
{
	UINT32 mem_type;
	unsigned long flags;

	if ((bar & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO) {
		return 0;
	}

	flags = bar & ~PCI_BASE_ADDRESS_MEM_MASK;
	flags |= IORESOURCE_MEM;
	if (flags & PCI_BASE_ADDRESS_MEM_PREFETCH)
		flags |= IORESOURCE_PREFETCH;

	mem_type = bar & PCI_BASE_ADDRESS_MEM_TYPE_MASK;
	switch (mem_type) {
	case PCI_BASE_ADDRESS_MEM_TYPE_32:
		break;
	case PCI_BASE_ADDRESS_MEM_TYPE_1M:
		/* 1M mem BAR treated as 32-bit BAR */
		break;
	case PCI_BASE_ADDRESS_MEM_TYPE_64:
		flags |= IORESOURCE_MEM_64;
		break;
	default:
		/* mem unknown type treated as 32-bit BAR */
		break;
	}
	return flags;
}

UINT64 pciSize(UINT64 base, UINT64 maxbase, UINT64 mask)
{
	UINT64 size = mask & maxbase;	/* Find the significant bits */
	if (!size)
		return 0;

	/*
	 * Get the lowest of them to find the decode size, and from that
	 * the extent.
	 */
	size = size & ~(size-1);

	/*
	 * base == maxbase can be valid only if the BAR has already been
	 * programmed with all 1s.
	 */
	if (base == maxbase && ((base | (size - 1)) & mask) != mask)
		return 0;

	return size;
}


void pciUpdateBAR(struct pciDev *dev, INTN resno, struct pciMemoryRange res)
{
	BOOLEAN disable;
	UINT16 cmd;
	UINT32 new, check, mask;
	INTN reg;
    UINT16 val;
	/*
	 * Ignore resources for unimplemented BARs and unused resource slots
	 * for 64 bit BARs.
	 */
	if (!res.flags)
		return;

	new = res.start;

	if (res.flags & IORESOURCE_IO) {
		mask = (UINT32)PCI_BASE_ADDRESS_IO_MASK;
		new |= res.flags & ~PCI_BASE_ADDRESS_IO_MASK;
	} else if (resno == PCI_ROM_RESOURCE) {
		mask = PCI_ROM_ADDRESS_MASK;
	} else {
		mask = (UINT32)PCI_BASE_ADDRESS_MEM_MASK;
		new |= res.flags & ~PCI_BASE_ADDRESS_MEM_MASK;
	}

	if (resno < PCI_ROM_RESOURCE) {
		reg = PCI_BASE_ADDRESS_0 + 4 * resno;
	} else if (resno == PCI_ROM_RESOURCE) {

		/*
		 * Apparently some Matrox devices have ROM BARs that read
		 * as zero when disabled, so don't update ROM BARs unless
		 * they're enabled.  See
		 * https://lore.kernel.org/r/43147B3D.1030309@vc.cvut.cz/
		 * But we must update ROM BAR for buggy devices where even a
		 * disabled ROM can conflict with other BARs.
		 */
		if (!(res.flags & IORESOURCE_ROM_ENABLE))
			return;

		reg = dev->romBaseReg;
		if (res.flags & IORESOURCE_ROM_ENABLE)
			new |= PCI_ROM_ADDRESS_ENABLE;
	} else
		return;

	/*
	 * We can't update a 64-bit BAR atomically, so when possible,
	 * disable decoding so that a half-updated BAR won't conflict
	 * with another device.
	 */
	disable = (res.flags & IORESOURCE_MEM_64) && !dev->isBridge;
	if (disable) {
		pciReadConfigWord(dev->pciAddress, PCI_COMMAND, &cmd);
		val = cmd & ~PCI_COMMAND_MEMORY;
		pciWriteConfigWord(dev->pciAddress, PCI_COMMAND, &val);
	}

	pciWriteConfigDword(dev->pciAddress, reg, &new);
	pciReadConfigDword(dev->pciAddress, reg, &check);

	if ((new ^ check) & mask) {
		Print(L"BAR %d: error updating (%#08x != %#08x)\n", resno, new, check);
	}

	if (res.flags & IORESOURCE_MEM_64) {
		new = res.start >> 16 >> 16;
		pciWriteConfigDword(dev->pciAddress, reg + 4, &new);
		pciReadConfigDword(dev->pciAddress, reg + 4, &check);
		if (check != new) {
			Print(L"BAR %d: error updating (high %#08x != %#08x)\n", resno, new, check);
		}
	}

	if (disable)
		pciWriteConfigWord(dev->pciAddress, PCI_COMMAND, &cmd);
}

void pciReadBridgeMMIO(struct pciDev *pDev)
{
	UINT16 mem_base_lo, mem_limit_lo;
	UINT64 base, limit;

	pciReadConfigWord(pDev->pciAddress, PCI_MEMORY_BASE, &mem_base_lo);
	pciReadConfigWord(pDev->pciAddress, PCI_MEMORY_LIMIT, &mem_limit_lo);
	base = ((UINT64) mem_base_lo & PCI_MEMORY_RANGE_MASK) << 16;
	limit = ((UINT64) mem_limit_lo & PCI_MEMORY_RANGE_MASK) << 16;
	if (base <= limit) {
		pDev->mmioWindow.flags = (mem_base_lo & PCI_MEMORY_RANGE_TYPE_MASK) | IORESOURCE_MEM;
		pDev->mmioWindow.start = base;
		pDev->mmioWindow.end = limit + 0xfffff;
	}
}

void pciReadBridgeMMIOPref(struct pciDev *pDev)
{
	UINT16 mem_base_lo, mem_limit_lo;
	UINT64 base64, limit64;
    // 64 bit
	UINT64 base, limit;

	pciReadConfigWord(pDev->pciAddress, PCI_PREF_MEMORY_BASE, &mem_base_lo);
	pciReadConfigWord(pDev->pciAddress, PCI_PREF_MEMORY_LIMIT, &mem_limit_lo);
	base64 = (mem_base_lo & PCI_PREF_RANGE_MASK) << 16;
	limit64 = (mem_limit_lo & PCI_PREF_RANGE_MASK) << 16;

	if ((mem_base_lo & PCI_PREF_RANGE_TYPE_MASK) == PCI_PREF_RANGE_TYPE_64) {
		UINT32 mem_base_hi, mem_limit_hi;

		pciReadConfigDword(pDev->pciAddress, PCI_PREF_BASE_UPPER32, &mem_base_hi);
		pciReadConfigDword(pDev->pciAddress, PCI_PREF_LIMIT_UPPER32, &mem_limit_hi);

		/*
		 * Some bridges set the base > limit by default, and some
		 * (broken) BIOSes do not initialize them.  If we find
		 * this, just assume they are not being used.
		 */
		if (mem_base_hi <= mem_limit_hi) {
			base64 |= (UINT64) mem_base_hi << 32;
			limit64 |= (UINT64) mem_limit_hi << 32;
		}
	}

	base = (UINT64) base64;
	limit = (UINT64) limit64;

	if (base <= limit) {
		pDev->mmioPrefWindow.flags = (mem_base_lo & PCI_PREF_RANGE_TYPE_MASK) |
					 IORESOURCE_MEM | IORESOURCE_PREFETCH;
		if (pDev->mmioPrefWindow.flags & PCI_PREF_RANGE_TYPE_64)
			pDev->mmioPrefWindow.flags |= IORESOURCE_MEM_64;
		pDev->mmioPrefWindow.start = base;
		pDev->mmioPrefWindow.end = limit + 0xfffff;
	}
}

void pciReadBridgeBus(struct pciDev *pDev) {
	UINT32 buses;
	pciReadConfigDword(pDev->pciAddress, PCI_PRIMARY_BUS, &buses);

	pDev->bridgeInfo.primary = buses & 0xFF;
	pDev->bridgeInfo.secondary = (buses >> 8) & 0xFF;
	pDev->bridgeInfo.subordinate = (buses >> 16) & 0xFF;
}

void pciSetupBridgeMMIO(struct pciDev *bridge)
{
	UINT32 l;

	if (bridge->mmioWindow.flags & IORESOURCE_MEM) {
		l = (bridge->mmioWindow.start >> 16) & 0xfff0;
		l |= bridge->mmioWindow.end & 0xfff00000;
	} else {
		l = 0x0000fff0;
	}
	pciWriteConfigDword(bridge->pciAddress, PCI_MEMORY_BASE, &l);
}

void pciSetupBridgeMMIOPref(struct pciDev *bridge)
{
	UINT32 l, bu, lu, val;

	/*
	 * Clear out the upper 32 bits of PREF limit.  If
	 * PCI_PREF_BASE_UPPER32 was non-zero, this temporarily disables
	 * PREF range, which is ok.
	 */
    val = 0;
	pciWriteConfigDword(bridge->pciAddress, PCI_PREF_LIMIT_UPPER32, &val);

	/* Set up PREF base/limit */
	bu = lu = 0;
	if (bridge->mmioPrefWindow.flags & IORESOURCE_PREFETCH) {
		l = (bridge->mmioPrefWindow.start >> 16) & 0xfff0;
		l |= bridge->mmioPrefWindow.end & 0xfff00000;
		if (bridge->mmioPrefWindow.flags & IORESOURCE_MEM_64) {
			bu = upper_32_bits(bridge->mmioPrefWindow.start);
			lu = upper_32_bits(bridge->mmioPrefWindow.end);
		}
	} else {
		l = 0x0000fff0;
	}
	pciWriteConfigDword(bridge->pciAddress, PCI_PREF_MEMORY_BASE, &l);

	/* Set the upper 32 bits of PREF base & limit */
	pciWriteConfigDword(bridge->pciAddress, PCI_PREF_BASE_UPPER32, &bu);
	pciWriteConfigDword(bridge->pciAddress, PCI_PREF_LIMIT_UPPER32, &lu);
}

void pciSetupBridge(struct pciDev *dev)
{
	pciSetupBridgeMMIO(dev);
	pciSetupBridgeMMIOPref(dev);

	//pci_write_config_word(bridge, PCI_BRIDGE_CONTROL, bus->bridge_ctl);
}

INTN pciReadBase(UINTN pciAddress, enum pci_bar_type type, UINTN pos, struct pciMemoryRange *barRange, BOOLEAN mmio_always_on) {
    UINT32 l = 0, sz = 0, mask;
	UINT64 l64 = 0, sz64 = 0, mask64 = 0;
	UINT16 orig_cmd;
    UINT32 val;

	mask = type ? PCI_ROM_ADDRESS_MASK : ~0;

    if (!mmio_always_on) {
        pciReadConfigWord(pciAddress, PCI_COMMAND, &orig_cmd);
        UINT16 cmd = orig_cmd & ~PCI_COMMAND_DECODE_ENABLE;
        pciWriteConfigWord(pciAddress, PCI_COMMAND, &cmd);
    }

    val = l | mask;
	pciReadConfigDword(pciAddress, pos, &l);
	pciWriteConfigDword(pciAddress, pos, &val);
	pciReadConfigDword(pciAddress, pos, &sz);
	pciWriteConfigDword(pciAddress, pos, &l);


    /*
	 * All bits set in sz means the device isn't working properly.
	 * If the BAR isn't implemented, all bits must be 0.  If it's a
	 * memory BAR or a ROM, bit 0 must be clear; if it's an io BAR, bit
	 * 1 must be clear.
	 */
	if (PCI_POSSIBLE_ERROR(sz))
		sz = 0;

	/*
	 * I don't know how l can have all bits set.  Copied from old code.
	 * Maybe it fixes a bug on some ancient platform.
	 */
	if (PCI_POSSIBLE_ERROR(l))
		l = 0;

	if (type == 0) {
		barRange->flags = decodeBar(l);
		barRange->flags |= IORESOURCE_SIZEALIGN;
		if (!(barRange->flags & IORESOURCE_IO)) {
			l64 = l & PCI_BASE_ADDRESS_MEM_MASK;
			sz64 = sz & PCI_BASE_ADDRESS_MEM_MASK;
			mask64 = (UINT32)PCI_BASE_ADDRESS_MEM_MASK;
		}
	} else {
		if (l & PCI_ROM_ADDRESS_ENABLE)
			barRange->flags |= IORESOURCE_ROM_ENABLE;
		l64 = l & PCI_ROM_ADDRESS_MASK;
		sz64 = sz & PCI_ROM_ADDRESS_MASK;
		mask64 = PCI_ROM_ADDRESS_MASK;
	}

	if (barRange->flags & IORESOURCE_MEM_64) {
        val = ~0;
		pciReadConfigDword(pciAddress, pos + 4, &l);
		pciWriteConfigDword(pciAddress, pos + 4, &val);
		pciReadConfigDword(pciAddress, pos + 4, &sz);
		pciWriteConfigDword(pciAddress, pos + 4, &l);

		l64 |= ((UINT64)l << 32);
		sz64 |= ((UINT64)sz << 32);
		mask64 |= ((UINT64)~0 << 32);
	}

    if (!mmio_always_on && (orig_cmd & PCI_COMMAND_DECODE_ENABLE)) {
        pciWriteConfigWord(pciAddress, PCI_COMMAND, &orig_cmd);
    }

    if (!sz64)
		goto fail;

    sz64 = pciSize(l64, sz64, mask64);
    if (!sz64)
		goto fail;

	barRange->start = l64;
	barRange->end = l64 + sz64 - 1;

    goto out;

fail:
    barRange->flags = 0;
out:
	return (barRange->flags & IORESOURCE_MEM_64) ? 1 : 0;
}

void pciReadBases(struct pciDev *pDev, UINTN howmany, INTN rom)
{
	UINTN pos, pos2, reg;

	for (pos = 0; pos < howmany; pos++) {
        struct pciMemoryRange barRange = {0};

		reg = PCI_BASE_ADDRESS_0 + (pos << 2);
        pos2 = pos + pciReadBase(pDev->pciAddress, pci_bar_unknown, reg, &barRange, pDev->isBridge);
        // we don't need IO ranges
        if (barRange.flags != 0 && ((barRange.flags & IORESOURCE_MEM) || (barRange.flags & IORESOURCE_MEM_64))) {
            pDev->bar[pos] = barRange;
			UINT32 rbarSizes = pciRebarGetPossibleSizes(pDev, pos);
			if (rbarSizes)
				pDev->bar[pos].resizableSize = fls(rbarSizes) - 1;
			else
				pDev->bar[pos].resizableSize = 0;
        }
        pos = pos2;
	}

	if (rom) {
        struct pciMemoryRange barRange = {0};
        pDev->romBaseReg = rom;
		barRange.flags = IORESOURCE_MEM | IORESOURCE_PREFETCH |
				IORESOURCE_READONLY | IORESOURCE_SIZEALIGN;
		pciReadBase(pDev->pciAddress, pci_bar_mem32, rom, &barRange, pDev->isBridge);
        if (barRange.flags != 0 && ((barRange.flags & IORESOURCE_MEM) || (barRange.flags & IORESOURCE_MEM_64))) {
            pDev->rom = barRange;
			UINT32 rbarSizes = pciRebarGetPossibleSizes(pDev, pos);
			if (rbarSizes)
				pDev->bar[pos].resizableSize = fls(rbarSizes) - 1;
			else
				pDev->bar[pos].resizableSize = 0;
        }
	}
}

VOID scanPCIDevices(UINT16 maxBus)
{
	UINT8 hdr_type;
	UINTN cnt = 0;
    UINTN bus, fun, dev;
    UINTN pciAddress;
    UINT16 vid;
	struct pciDev *pciDevList;
                

	pciDevList = AllocatePool((maxBus * PCI_MAX_DEVICE * PCI_MAX_FUNC) * sizeof(struct pciDev));
    for (bus = 0; bus <= maxBus; bus++)
        for (dev = 0; dev <= PCI_MAX_DEVICE; dev++)
            for (fun = 0; fun <= PCI_MAX_FUNC; fun++)
            {
                struct pciDev pDev = {0};
                pciAddress = EFI_PCI_ADDRESS(bus, dev, fun, 0);
                pciReadConfigWord(pciAddress, 0, &vid);

                if (vid == 0xFFFF)
                    continue;
				
				pciReadConfigWord(pciAddress, 2, &pDev.did);

				pDev.vid = vid;
				pDev.pciRootBridgeIo = pciRootBridgeIo;
				pDev.pciAddress = pciAddress;

				pciReadConfigByte(pciAddress, PCI_HEADER_TYPE, &hdr_type);
                switch (hdr_type & 0x7f)
                {

				case PCI_HEADER_TYPE_NORMAL:
					pciReadBases(&pDev, 6, PCI_ROM_ADDRESS);
					break;
				case PCI_HEADER_TYPE_BRIDGE:
                    pDev.isBridge = TRUE;
				 	pciReadBases(&pDev, 2, PCI_ROM_ADDRESS1);
					pciReadBridgeMMIO(&pDev);
					pciReadBridgeMMIOPref(&pDev);
					pciReadBridgeBus(&pDev);
					break;
				case PCI_HEADER_TYPE_CARDBUS:
					pciReadBases(&pDev, 1, 0);
					break;  

				default:
					break;
                }

                cnt++;
                pciDevList[cnt - 1] = pDev;

                if (!fun && ((hdr_type & HEADER_TYPE_MULTI_FUNCTION) == 0))
                    break; // no
            }

	#ifdef DXE
    UINT16 cmd, val = 0;
    UINT64 pDevAddr = 0;
	#endif
    for (UINTN i = 0; i < cnt; i++)
    {
        struct pciDev pDev = pciDevList[i];

		// CHANGE THESE FOR YOUR SYSTEM
        if (!pDev.isBridge) {
			UINT64 nSize;
			struct pciMemoryRange Mwindow;
			BOOLEAN resizableDev = FALSE;
			for (UINTN j = 0; j < 6; j++)
        	{
				struct pciMemoryRange window;
				struct pciMemoryRange barRange = pDev.bar[j];
            	if (barRange.start != 0 && (barRange.resizableSize != 0 || resizableDev)) {
					// find bridge device is on
					for (UINTN i = 0; i < cnt; i++) {
						struct pciDev pDevB = pciDevList[i];
						if (pDevB.isBridge) {
    						UINT8 bus = (pDev.pciAddress & 0xff000000) >> 24;
							if (bus >= pDevB.bridgeInfo.secondary && bus <= pDevB.bridgeInfo.subordinate) {
								Print(L"BAR %u [0x%llx-0x%llx] resizable: %u\n", j, barRange.start, barRange.end, barRange.resizableSize);
								if (pDevB.mmioWindow.start <= pDev.bar[j].start && pDev.bar[j].end <= pDevB.mmioWindow.end) {
									window = pDevB.mmioWindow;
									Print(L"MMIO [0x%llx-0x%llx]\n", pDevB.mmioWindow.start, pDevB.mmioWindow.end);
								}

								if (pDevB.mmioPrefWindow.start <= pDev.bar[j].start && pDev.bar[j].end <= pDevB.mmioPrefWindow.end) {
									window = pDevB.mmioPrefWindow;
									Print(L"MMIOPref [0x%llx-0x%llx]\n", pDevB.mmioPrefWindow.start, pDevB.mmioPrefWindow.end);
								}
							}
						}
					}

					if (!resizableDev) {
						Mwindow = window;
						resizableDev = TRUE;
						nSize = (1 << barRange.resizableSize);
						nSize = nSize * 1024 * 1024;
						pDev.bar[j].start = 0;
						pDev.bar[j].end = 0;
					} else {
						if (window.start == Mwindow.start) {
							nSize += pDev.bar[j].end - pDev.bar[j].start;
							pDev.bar[j].start = 0;
							pDev.bar[j].end = 0;
						}
					}
					Print(L"Size correction needed %llu\n", nSize);
				}
			}
			
/*             if (pDev.bar[0].start == 0x600000000) {
				#ifdef DXE
                pDevAddr = pDev.pciAddress;
				#endif
				pDev.bar[0].start = 0x400000000;
                pDev.bar[2].start = 0x300000000;

				#ifdef DXE
                pciReadConfigWord(pDev.pciAddress, PCI_COMMAND, &cmd);
                val = cmd & ~PCI_COMMAND_MEMORY;
                pciWriteConfigWord(pDev.pciAddress, PCI_COMMAND, &val);

				pciUpdateBAR(&pDev, 0, pDev.bar[0]);
                pciUpdateBAR(&pDev, 2, pDev.bar[2]);
				#endif

            } */
        } else {
/*             if (pDev.mmioPrefWindow.start == 0x600000000) {
                pDev.mmioPrefWindow.start = 0x300000000;
                pDev.mmioPrefWindow.end = 0x5ffffffff;
				#ifdef DXE
                pciSetupBridgeMMIOPref(&pDev);
				#endif
            } */
        }
/*         for (UINTN j = 0; j < 6; j++)
        {
            struct pciMemoryRange barRange = pDev.bar[j];
            if (barRange.start != 0)
                Print(L"BAR %u [0x%llx-0x%llx] resizable: %u\n", j, barRange.start, barRange.end, barRange.resizableSize);
        }
        if (pDev.rom.start != 0)
            Print(L"BAR 6 [0x%llx-0x%llx] resizable: %u\n", pDev.rom.start, pDev.rom.end, pDev.rom.resizableSize);
        
        if (pDev.isBridge) {
            if (pDev.mmioWindow.start != 0)
                Print(L"MMIO [0x%llx-0x%llx]\n", pDev.mmioWindow.start, pDev.mmioWindow.end);
            if (pDev.mmioPrefWindow.start != 0)
                Print(L"MMIOPref [0x%llx-0x%llx]\n", pDev.mmioPrefWindow.start, pDev.mmioPrefWindow.end);


			Print(L"Primary %u, Secondary %u, Subordinate %u\n", pDev.bridgeInfo.primary, pDev.bridgeInfo.secondary, pDev.bridgeInfo.subordinate);
        }  */
    }

	#ifdef DXE
    if (pDevAddr) {
        INTN rbarsize = pciRebarGetCurrentSize(pDevAddr, 0);
        UINT32 rbarsizes = pciRebarGetPossibleSizes(pDevAddr, 0);
        UINTN maxsize = fls(rbarsizes) - 1;

        if (rbarsize > 0) {
            pciRebarSetSize(pDevAddr, 0, maxsize);
            rbarsize = pciRebarGetCurrentSize(pDevAddr, 0);
        }

        pciWriteConfigWord(pDevAddr, PCI_COMMAND, &cmd);
    }
	#endif

/*     EFI_LOADED_IMAGE* image;
    EFI_HANDLE next_image_handle = 0;

    gBS->HandleProtocol(iHandle, &gEfiLoadedImageProtocolGuid, (void**) &image);

    static CHAR16 default_boot_path[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
    EFI_DEVICE_PATH* boot_dp = FileDevicePath(image->DeviceHandle, default_boot_path);
    gBS->LoadImage(0, iHandle, boot_dp, 0, 0, &next_image_handle);
    gBS->StartImage(next_image_handle, 0, 0); */

    FreePool(pciDevList);
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
    iHandle = imageHandle;
    status = gBS->LocateHandleBuffer(
        ByProtocol,
        &gEfiPciRootBridgeIoProtocolGuid,
        NULL,
        &handleCount,
        &handleBuffer);
    ASSERT_EFI_ERROR(Status);

    for (i = 0; i < handleCount; i++)
    {
        status = gBS->HandleProtocol(handleBuffer[i], &gEfiPciRootBridgeIoProtocolGuid, (void **)&pciRootBridgeIo);
        ASSERT_EFI_ERROR(status);
        status = pciRootBridgeIo->Configuration (pciRootBridgeIo, (VOID **)&descriptors);
        ASSERT_EFI_ERROR(status);

        while (TRUE) {
            status = PciGetNextBusRange (&descriptors, &minBus, &maxBus, &isEnd);
            ASSERT_EFI_ERROR(status);

            if (isEnd || descriptors == NULL)
                break;
            scanPCIDevices(maxBus);
        }
    }

    FreePool(handleBuffer);

    return EFI_SUCCESS;
}
