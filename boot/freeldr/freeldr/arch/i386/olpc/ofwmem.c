/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Retrieving a memory map from Open Firmware
 * COPYRIGHT:   Copyright 2007-2009 Aleksey Bragin (aleksey@reactos.org)
 *              Copyright 2023 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(MEMORY);

/* GLOBALS ********************************************************************/

static FREELDR_MEMORY_DESCRIPTOR OFwpMemoryMap[MAX_BIOS_DESCRIPTORS + 1];

/* FUNCTIONS ******************************************************************/

/* pcmem.c */
extern VOID
SetMemory(
    PFREELDR_MEMORY_DESCRIPTOR MemoryMap,
    ULONG_PTR BaseAddress,
    SIZE_T Size,
    TYPE_OF_MEMORY MemoryType);

/* pcmem.c */
extern VOID
ReserveMemory(
    PFREELDR_MEMORY_DESCRIPTOR MemoryMap,
    ULONG_PTR BaseAddress,
    SIZE_T Size,
    TYPE_OF_MEMORY MemoryType,
    PCHAR Usage);

/* pcmem.c */
extern ULONG
PcMemFinalizeMemoryMap(PFREELDR_MEMORY_DESCRIPTOR MemoryMap);

PFREELDR_MEMORY_DESCRIPTOR
OFwMemGetMemoryMap(
    ULONG* MemoryMapSize)
{
    OFW_PHANDLE MemoryHandle;
    ULONG AvailableMemory[64], i;
    ULONG PropertySize;

    if (!OFwFindDevice("/memory", &MemoryHandle))
        return NULL;

    if (!OFwGetProperty(MemoryHandle,
                        "available",
                        &AvailableMemory,
                        sizeof(AvailableMemory),
                        &PropertySize))
    {
        return NULL;
    }

    RtlZeroMemory(&PcBiosMemoryMap, sizeof(BIOS_MEMORY_MAP) * MAX_BIOS_DESCRIPTORS);
    RtlZeroMemory(&OFwpMemoryMap, sizeof(OFwpMemoryMap));
    PcBiosMapCount = 0;

    /* Setup allowed ranges */
    for (i = 0; i < PropertySize; i += 2)
    {
        ULONG Base = RtlUlongByteSwap(AvailableMemory[i]);
        ULONG Length = RtlUlongByteSwap(AvailableMemory[i + 1]);

        if (Length == 0)
            continue;

        TRACE("Base %08lx, %x (%u)\n", Base, Length, Length);

#if 0 // FIXME: This leads to the RAM being unavailable to OFW (?)
        if (!OFwClaim(Base, Length, 0, NULL))
        {
            TRACE("Claim failed\n");
            continue;
        }
#endif

        SetMemory(OFwpMemoryMap, Base, Length, LoaderFree);
    }

    // TODO: Find and map firmware

    *MemoryMapSize = PcMemFinalizeMemoryMap(OFwpMemoryMap);

    return OFwpMemoryMap;
}
