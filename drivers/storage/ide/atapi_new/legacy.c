/*
 * PROJECT:     ReactOS IDE Port Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Legacy (non-PnP) IDE controllers support
 * COPYRIGHT:   Copyright 2020 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include "atapi.h"

// #define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/



/* FUNCTIONS ******************************************************************/

// static
CODE_SEG("INIT")
VOID
FillResourceList(
    _In_ ULONG IoBase,
    _In_ ULONG RawVector,
    _Out_ PCM_RESOURCE_LIST ResourceList)
{
    /* FIXME */ volatile
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG i, j = 0; (i);

#if defined(SARCH_PC98)
    /* C-Bus */
    for (i = 0; i < 8; i++)
    {
        Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[j++];
        Descriptor->Type = CmResourceTypePort;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->Flags = CM_RESOURCE_PORT_IO | CM_RESOURCE_PORT_10_BIT_DECODE;
        Descriptor->u.Port.Start.u.LowPart = IoBase + i * 2;
        Descriptor->u.Port.Length = 1;
    }

    Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[j++];
    Descriptor->Type = CmResourceTypePort;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_PORT_IO | CM_RESOURCE_PORT_10_BIT_DECODE;
    Descriptor->u.Port.Start.u.LowPart = IoBase + 0x10C;
    Descriptor->u.Port.Length = 1;

    Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[j++];
    Descriptor->Type = CmResourceTypePort;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_PORT_IO | CM_RESOURCE_PORT_10_BIT_DECODE;
    Descriptor->u.Port.Start.u.LowPart = 0x432;
    Descriptor->u.Port.Length = 1;

    Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[j++];
    Descriptor->Type = CmResourceTypePort;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_PORT_IO | CM_RESOURCE_PORT_10_BIT_DECODE;
    Descriptor->u.Port.Start.u.LowPart = 0x435;
    Descriptor->u.Port.Length = 1;
#else
    /* ISA */
    Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[j++];
    Descriptor->Type = CmResourceTypePort;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_PORT_IO | CM_RESOURCE_PORT_10_BIT_DECODE;
    Descriptor->u.Port.Start.u.LowPart = IoBase;
    Descriptor->u.Port.Length = 8;

    Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[j++];
    Descriptor->Type = CmResourceTypePort;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_PORT_IO | CM_RESOURCE_PORT_10_BIT_DECODE;
    Descriptor->u.Port.Start.u.LowPart = IoBase + 0x206;
    Descriptor->u.Port.Length = 1;
#endif
    Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[j++];
    Descriptor->Type = CmResourceTypeInterrupt;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
    Descriptor->u.Interrupt.Level = RawVector;
    Descriptor->u.Interrupt.Vector = RawVector;
    Descriptor->u.Interrupt.Affinity = 0xFFFFFFFF;
}

CODE_SEG("INIT")
VOID
DetectLegacyDevices(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UCHAR BusNumber;
    PCM_RESOURCE_LIST ResourceList;
    ULONG ResourceCount, ResourceListSize;
#if defined(SARCH_XBOX)
    const ULONG IoBaseArray[] = { 0x1F0 };
    const ULONG IrqArray[] = { 14 };
#elif defined(SARCH_PC98)
    const ULONG IoBaseArray[] = { 0x640 };
    const ULONG IrqArray[] = { 9 };
#else
    const ULONG IoBaseArray[] = { 0x1F0, 0x170, 0x1E8, 0x168 /*, 0x1E0, 0x160 */ };
    const ULONG IrqArray[] = { 14, 15, 11, 10 /*, 8, 12 */ };
    // https://86box.readthedocs.io/en/latest/hardware/ideterqua.html
#endif

#if defined(SARCH_PC98)
    ResourceCount = 12;
#else
    ResourceCount = 3;
#endif
    ResourceListSize = sizeof(CM_RESOURCE_LIST) +
                       (ResourceCount - 1) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    ResourceList = ExAllocatePoolZero(PagedPool, ResourceListSize, IDEPORT_TAG);
    if (!ResourceList)
    {
        ERR("Failed to allocate the resource list\n");
        return;
    }
    ResourceList->Count = 1;
    ResourceList->List[0].InterfaceType = Isa;
    ResourceList->List[0].BusNumber = 0;
    ResourceList->List[0].PartialResourceList.Version = 1;
    ResourceList->List[0].PartialResourceList.Revision = 1;
    ResourceList->List[0].PartialResourceList.Count = ResourceCount;

    for (BusNumber = 0;
         BusNumber < RTL_NUMBER_OF(IoBaseArray);
         ++BusNumber)
    {
        INFO("IDE Channel %d", BusNumber);

        FillResourceList(IoBaseArray[BusNumber], IrqArray[BusNumber], ResourceList);
    }
}
