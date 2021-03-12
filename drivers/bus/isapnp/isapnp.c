/*
 * PROJECT:         ReactOS ISA PnP Bus driver
 * PURPOSE:         Driver entry
 * COPYRIGHT:       Copyright 2010 Cameron Gutman (cameron.gutman@reactos.org)
 *                  Copyright 2020 Hervé Poussineau (hpoussin@reactos.org)
 *                  Copyright 2021 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include <isapnp.h>

#include <search.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

BOOLEAN ReadPortCreated = FALSE;

_Has_lock_kind_(_Lock_kind_event_)
KEVENT BusSyncEvent;

_Guarded_by_(BusSyncEvent)
LIST_ENTRY BusListHead;

static PUCHAR Priority;

/* FUNCTIONS ******************************************************************/

static
CODE_SEG("PAGE")
int
IsaComparePriority(
    const void *A,
    const void *B)
{
    return Priority[*(PUCHAR)A] - Priority[*(PUCHAR)B];
}

static
CODE_SEG("PAGE")
VOID
IsaDetermineBestConfig(
    _Out_ PUCHAR BestConfig,
    _In_ PISAPNP_ALTERNATIVES Alternatives)
{
    ULONG i;

    PAGED_CODE();

    for (i = 0; i < ISAPNP_MAX_ALTERNATIVES; i++)
        BestConfig[i] = i;

    Priority = Alternatives->Priority;
    qsort(BestConfig,
          Alternatives->Count,
          sizeof(UCHAR),
          IsaComparePriority);
}

static
CODE_SEG("PAGE")
VOID
IsaConvertIoDescription(
    _Out_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ PISAPNP_IO_DESCRIPTION Description)
{
    PAGED_CODE();

    Descriptor->Type = CmResourceTypePort;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_PORT_IO;
    if (Description->Information & 0x1)
        Descriptor->Flags |= CM_RESOURCE_PORT_16_BIT_DECODE;
    else
        Descriptor->Flags |= CM_RESOURCE_PORT_10_BIT_DECODE;
    Descriptor->u.Port.Length = Description->Length;
    Descriptor->u.Port.Alignment = Description->Alignment;
    Descriptor->u.Port.MinimumAddress.LowPart = Description->Minimum;
    Descriptor->u.Port.MaximumAddress.LowPart = Description->Maximum +
                                                             Description->Length - 1;
}

static
CODE_SEG("PAGE")
VOID
IsaConvertIrqDescription(
    _Out_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ PISAPNP_IRQ_DESCRIPTION Description,
    _In_ ULONG Vector,
    _In_ BOOLEAN FirstDescriptor)
{
    PAGED_CODE();

    if (!FirstDescriptor)
        Descriptor->Option = IO_RESOURCE_ALTERNATIVE;
    Descriptor->Type = CmResourceTypeInterrupt;
    if (Description->Information & 0xC)
    {
        Descriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        Descriptor->ShareDisposition = CmResourceShareShared;
    }
    else
    {
        Descriptor->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    }
    Descriptor->u.Interrupt.MinimumVector =
    Descriptor->u.Interrupt.MaximumVector = Vector;
}

static
CODE_SEG("PAGE")
VOID
IsaConvertDmaDescription(
    _Out_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ PISAPNP_DMA_DESCRIPTION Description,
    _In_ ULONG Channel,
    _In_ BOOLEAN FirstDescriptor)
{
    PAGED_CODE();

    if (!FirstDescriptor)
        Descriptor->Option = IO_RESOURCE_ALTERNATIVE;
    Descriptor->Type = CmResourceTypeDma;
    Descriptor->ShareDisposition = CmResourceShareUndetermined;
    Descriptor->Flags = CM_RESOURCE_DMA_8; /* Information byte is ignored */
    Descriptor->u.Dma.MinimumChannel =
    Descriptor->u.Dma.MaximumChannel = Channel;
}

static
CODE_SEG("PAGE")
VOID
IsaConvertMemRangeDescription(
    _Out_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ PISAPNP_MEMRANGE_DESCRIPTION Description)
{
    PAGED_CODE();

    Descriptor->Type = CmResourceTypeMemory;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_MEMORY_24; /* Information byte is ignored */
    Descriptor->u.Memory.Length = Description->Length << 8;
    if (Description->Alignment == 0)
        Descriptor->u.Memory.Alignment = 0x10000;
    else
        Descriptor->u.Memory.Alignment = Description->Alignment;
    Descriptor->u.Memory.MinimumAddress.LowPart = Description->Minimum << 8;
    Descriptor->u.Memory.MaximumAddress.LowPart = (Description->Maximum << 8) +
                                                  (Description->Length << 8) - 1;
}

static
CODE_SEG("PAGE")
VOID
IsaConvertMemRange32Description(
    _Out_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ PISAPNP_MEMRANGE32_DESCRIPTION Description)
{
    PAGED_CODE();

    Descriptor->Type = CmResourceTypeMemory;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_MEMORY_24; /* Information byte is ignored */
    Descriptor->u.Memory.Length = Description->Length;
    Descriptor->u.Memory.Alignment = Description->Alignment;
    Descriptor->u.Memory.MinimumAddress.LowPart = Description->Minimum;
    Descriptor->u.Memory.MaximumAddress.LowPart = Description->Maximum +
                                                  Description->Length - 1;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaFdoCreateRequirements(
    _In_ PISAPNP_PDO_EXTENSION PdoExt)
{
    PISAPNP_LOGICAL_DEVICE LogDev = PdoExt->IsaPnpDevice;
    RTL_BITMAP IrqBitmap[RTL_NUMBER_OF(LogDev->Irq)];
    RTL_BITMAP DmaBitmap[RTL_NUMBER_OF(LogDev->Dma)];
    ULONG IrqData[RTL_NUMBER_OF(LogDev->Irq)];
    ULONG DmaData[RTL_NUMBER_OF(LogDev->Dma)];
    ULONG ResourceCount = 0, AltCount = 0, AltOptionalCount = 0;
    ULONG ListSize, i, j;
    BOOLEAN FirstDescriptor;
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    PISAPNP_ALTERNATIVES Alternatives = LogDev->Alternatives;

    PAGED_CODE();

    /* Count number of requirements */
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Io); i++)
    {
        if (!LogDev->Io[i].Description.Length)
            break;

        ResourceCount++;
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Irq); i++)
    {
        if (!LogDev->Irq[i].Description.Mask)
            break;

        IrqData[i] = LogDev->Irq[i].Description.Mask;
        RtlInitializeBitMap(&IrqBitmap[i],
                            &IrqData[i],
                            RTL_BITS_OF(LogDev->Irq[i].Description.Mask));
        ResourceCount += RtlNumberOfSetBits(&IrqBitmap[i]);
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Irq); i++)
    {
        if (!LogDev->Dma[i].Description.Mask)
            break;

        DmaData[i] = LogDev->Dma[i].Description.Mask;
        RtlInitializeBitMap(&DmaBitmap[i],
                            &DmaData[i],
                            RTL_BITS_OF(LogDev->Dma[i].Description.Mask));
        ResourceCount += RtlNumberOfSetBits(&DmaBitmap[i]);
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->MemRange); i++)
    {
        if (!LogDev->MemRange[i].Description.Length)
            break;

        ResourceCount++;
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->MemRange32); i++)
    {
        if (!LogDev->MemRange32[i].Description.Length)
            break;

        ResourceCount++;
    }
    if (Alternatives)
    {
        RTL_BITMAP TempBitmap;
        ULONG TempBuffer;
        UCHAR BitCount;

        if (Alternatives->Io[0].Length)
            AltCount++;
        if (Alternatives->Irq[0].Mask)
            AltCount++;
        if (Alternatives->Dma[0].Mask)
            AltCount++;
        if (Alternatives->MemRange[0].Length)
            AltCount++;
        if (Alternatives->MemRange32[0].Length)
            AltCount++;
        ResourceCount += AltCount;

        if (Alternatives->Irq[0].Mask)
        {
            for (i = 0; i < Alternatives->Count; i++)
            {
                TempBuffer = Alternatives->Irq[i].Mask;
                RtlInitializeBitMap(&TempBitmap,
                                    &TempBuffer,
                                    RTL_BITS_OF(Alternatives->Irq[i].Mask));
                BitCount = RtlNumberOfSetBits(&TempBitmap);

                if (BitCount > 1)
                    AltOptionalCount += BitCount - 1;
            }
        }
        if (Alternatives->Dma[0].Mask)
        {
            for (i = 0; i < Alternatives->Count; i++)
            {
                TempBuffer = Alternatives->Dma[i].Mask;
                RtlInitializeBitMap(&TempBitmap,
                                    &TempBuffer,
                                    RTL_BITS_OF(Alternatives->Dma[i].Mask));
                BitCount = RtlNumberOfSetBits(&TempBitmap);

                if (BitCount > 1)
                    AltOptionalCount += BitCount - 1;
            }
        }
    }
    if (ResourceCount == 0)
        return STATUS_SUCCESS;

    /* Allocate memory to store requirements */
    ListSize = sizeof(IO_RESOURCE_REQUIREMENTS_LIST);
    if (Alternatives)
    {
        ListSize += sizeof(IO_RESOURCE_DESCRIPTOR) * (ResourceCount - 1) * Alternatives->Count
                    + sizeof(IO_RESOURCE_LIST) * (Alternatives->Count - 1) +
                    + sizeof(IO_RESOURCE_DESCRIPTOR) * AltOptionalCount;
    }
    else
    {
        ListSize += sizeof(IO_RESOURCE_DESCRIPTOR) * (ResourceCount - 1);
    }
    RequirementsList = ExAllocatePoolZero(PagedPool, ListSize, TAG_ISAPNP);
    if (!RequirementsList)
        return STATUS_NO_MEMORY;

    RequirementsList->ListSize = ListSize;
    RequirementsList->InterfaceType = Isa;
    RequirementsList->AlternativeLists = Alternatives ? Alternatives->Count : 1;

    RequirementsList->List[0].Version = 1;
    RequirementsList->List[0].Revision = 1;
    RequirementsList->List[0].Count = ResourceCount;

    /* Store requirements */
    Descriptor = RequirementsList->List[0].Descriptors;
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Io); i++)
    {
        if (!LogDev->Io[i].Description.Length)
            break;

        IsaConvertIoDescription(Descriptor++, &LogDev->Io[i].Description);
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Irq); i++)
    {
        if (!LogDev->Irq[i].Description.Mask)
            break;

        FirstDescriptor = TRUE;

        for (j = 0; j < RTL_BITS_OF(LogDev->Irq[i].Description.Mask); j++)
        {
            if (!RtlCheckBit(&IrqBitmap[i], j))
                continue;

            IsaConvertIrqDescription(Descriptor++,
                                     &LogDev->Irq[i].Description,
                                     j,
                                     FirstDescriptor);

            if (FirstDescriptor)
                FirstDescriptor = FALSE;
        }
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Dma); i++)
    {
        if (!LogDev->Dma[i].Description.Mask)
            break;

        FirstDescriptor = TRUE;

        for (j = 0; j < RTL_BITS_OF(LogDev->Dma[i].Description.Mask); j++)
        {
            if (!RtlCheckBit(&DmaBitmap[i], j))
                continue;

            IsaConvertDmaDescription(Descriptor++,
                                     &LogDev->Dma[i].Description,
                                     j,
                                     FirstDescriptor);

            if (FirstDescriptor)
                FirstDescriptor = FALSE;
        }
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->MemRange); i++)
    {
        if (!LogDev->MemRange[i].Description.Length)
            break;

        IsaConvertMemRangeDescription(Descriptor++,
                                      &LogDev->MemRange[i].Description);
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->MemRange32); i++)
    {
        if (!LogDev->MemRange32[i].Description.Length)
            break;

        IsaConvertMemRange32Description(Descriptor++,
                                        &LogDev->MemRange32[i].Description);
    }
    if (Alternatives)
    {
        UCHAR BestConfig[ISAPNP_MAX_ALTERNATIVES];
        PIO_RESOURCE_LIST AltList = &RequirementsList->List[0];
        PIO_RESOURCE_LIST NextList = AltList;
        RTL_BITMAP TempBitmap;
        ULONG TempBuffer;

        IsaDetermineBestConfig(BestConfig, Alternatives);

        for (i = 0; i < RequirementsList->AlternativeLists; i++)
        {
            RtlMoveMemory(NextList, AltList, sizeof(IO_RESOURCE_LIST));

            /* Just because the 'NextList->Count++' correction */
            NextList->Count = ResourceCount;

            /* Propagate the fixed resources to our new list */
            for (j = 0; j < AltList->Count - AltCount; j++)
            {
                RtlMoveMemory(&NextList->Descriptors[j],
                              &AltList->Descriptors[j],
                              sizeof(IO_RESOURCE_DESCRIPTOR));
            }

            Descriptor = &NextList->Descriptors[NextList->Count - AltCount];

            /* Append alternatives */
            if (Alternatives->Io[0].Length)
            {
                IsaConvertIoDescription(Descriptor++,
                                        &Alternatives->Io[BestConfig[i]]);
            }
            if (Alternatives->Irq[0].Mask)
            {
                TempBuffer = Alternatives->Irq[BestConfig[i]].Mask;
                RtlInitializeBitMap(&TempBitmap,
                                    &TempBuffer,
                                    RTL_BITS_OF(Alternatives->Irq[BestConfig[i]].Mask));

                FirstDescriptor = TRUE;

                for (j = 0; j < RTL_BITS_OF(Alternatives->Irq[BestConfig[i]].Mask); j++)
                {
                    if (!RtlCheckBit(&TempBitmap, j))
                        continue;

                    IsaConvertIrqDescription(Descriptor++,
                                             &Alternatives->Irq[BestConfig[i]],
                                             j,
                                             FirstDescriptor);

                    if (FirstDescriptor)
                        FirstDescriptor = FALSE;
                    else
                        NextList->Count++;
                }
            }
            if (Alternatives->Dma[0].Mask)
            {
                TempBuffer = Alternatives->Dma[BestConfig[i]].Mask;
                RtlInitializeBitMap(&TempBitmap,
                                    &TempBuffer,
                                    RTL_BITS_OF(Alternatives->Dma[BestConfig[i]].Mask));

                FirstDescriptor = TRUE;

                for (j = 0; j < RTL_BITS_OF(Alternatives->Dma[BestConfig[i]].Mask); j++)
                {
                    if (!RtlCheckBit(&TempBitmap, j))
                        continue;

                    IsaConvertDmaDescription(Descriptor++,
                                             &Alternatives->Dma[BestConfig[i]],
                                             j,
                                             FirstDescriptor);

                    if (FirstDescriptor)
                        FirstDescriptor = FALSE;
                    else
                        NextList->Count++;
                }
            }
            if (Alternatives->MemRange[0].Length)
            {
                IsaConvertMemRangeDescription(Descriptor++,
                                              &Alternatives->MemRange[BestConfig[i]]);
            }
            if (Alternatives->MemRange32[0].Length)
            {
                IsaConvertMemRange32Description(Descriptor++,
                                                &Alternatives->MemRange32[BestConfig[i]]);
            }

            NextList = (PIO_RESOURCE_LIST)(NextList->Descriptors + NextList->Count);
        }
    }

    PdoExt->RequirementsList = RequirementsList;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaFdoCreateResources(
    _In_ PISAPNP_PDO_EXTENSION PdoExt)
{
    PISAPNP_LOGICAL_DEVICE LogDev = PdoExt->IsaPnpDevice;
    ULONG ResourceCount = 0;
    ULONG ListSize, i;
    PCM_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;

    PAGED_CODE();

    if (!(LogDev->Flags & ISAPNP_HAS_RESOURCES))
        return STATUS_SUCCESS;

    /* Count number of required resources */
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Io); i++)
    {
        if (LogDev->Io[i].CurrentBase)
            ResourceCount++;
        else
            break;
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Irq); i++)
    {
        if (LogDev->Irq[i].CurrentNo)
            ResourceCount++;
        else
            break;
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Dma); i++)
    {
        if (LogDev->Dma[i].CurrentChannel != 4)
            ResourceCount++;
        else
            break;
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->MemRange); i++)
    {
        if (LogDev->MemRange[i].CurrentBase)
            ResourceCount++;
        else
            break;
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->MemRange32); i++)
    {
        if (LogDev->MemRange32[i].CurrentBase)
            ResourceCount++;
        else
            break;
    }
    if (ResourceCount == 0)
        return STATUS_SUCCESS;

    /* Allocate memory to store resources */
    ListSize = sizeof(CM_RESOURCE_LIST)
               + (ResourceCount - 1) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    ResourceList = ExAllocatePoolZero(PagedPool, ListSize, TAG_ISAPNP);
    if (!ResourceList)
        return STATUS_NO_MEMORY;

    ResourceList->Count = 1;
    ResourceList->List[0].InterfaceType = Isa;
    ResourceList->List[0].PartialResourceList.Version = 1;
    ResourceList->List[0].PartialResourceList.Revision = 1;
    ResourceList->List[0].PartialResourceList.Count = ResourceCount;

    /* Store resources */
    ResourceCount = 0;
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Io); i++)
    {
        if (!LogDev->Io[i].CurrentBase)
            break;

        Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[ResourceCount++];
        Descriptor->Type = CmResourceTypePort;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->Flags = CM_RESOURCE_PORT_IO;
        if (LogDev->Io[i].Description.Information & 0x1)
            Descriptor->Flags |= CM_RESOURCE_PORT_16_BIT_DECODE;
        else
            Descriptor->Flags |= CM_RESOURCE_PORT_10_BIT_DECODE;
        Descriptor->u.Port.Length = LogDev->Io[i].Description.Length;
        Descriptor->u.Port.Start.LowPart = LogDev->Io[i].CurrentBase;
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Irq); i++)
    {
        if (!LogDev->Irq[i].CurrentNo)
            break;

        Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[ResourceCount++];
        Descriptor->Type = CmResourceTypeInterrupt;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        if (LogDev->Irq[i].CurrentType & 0x01)
            Descriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        else
            Descriptor->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
        Descriptor->u.Interrupt.Level = LogDev->Irq[i].CurrentNo;
        Descriptor->u.Interrupt.Vector = LogDev->Irq[i].CurrentNo;
        Descriptor->u.Interrupt.Affinity = -1;
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->Dma); i++)
    {
        if (LogDev->Dma[i].CurrentChannel == 4)
            break;

        Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[ResourceCount++];
        Descriptor->Type = CmResourceTypeDma;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->Flags = CM_RESOURCE_DMA_8; /* Information byte is ignored */
        Descriptor->u.Dma.Channel = LogDev->Dma[i].CurrentChannel;
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->MemRange); i++)
    {
        if (!LogDev->MemRange[i].CurrentBase)
            break;

        Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[ResourceCount++];
        Descriptor->Type = CmResourceTypeMemory;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->Flags = CM_RESOURCE_MEMORY_24; /* Information byte is ignored */
        Descriptor->u.Memory.Length = LogDev->MemRange[i].Description.Length;
        Descriptor->u.Memory.Start.QuadPart = LogDev->MemRange[i].CurrentBase;
    }
    for (i = 0; i < RTL_NUMBER_OF(LogDev->MemRange32); i++)
    {
        if (!LogDev->MemRange32[i].CurrentBase)
            break;

        Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[ResourceCount++];
        Descriptor->Type = CmResourceTypeMemory;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->Flags = CM_RESOURCE_MEMORY_24; /* Information byte is ignored */
        Descriptor->u.Memory.Length = LogDev->MemRange32[i].Description.Length;
        Descriptor->u.Memory.Start.QuadPart = LogDev->MemRange32[i].CurrentBase;
    }

    PdoExt->ResourceList = ResourceList;
    PdoExt->ResourceListSize = ListSize;
    return STATUS_SUCCESS;
}

static DRIVER_DISPATCH_PAGED IsaCreateClose;

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
IsaCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAGED_CODE();

    Irp->IoStatus.Status = STATUS_SUCCESS;

    DPRINT("%s(%p, %p)\n", __FUNCTION__, DeviceObject, Irp);

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

static DRIVER_DISPATCH_PAGED IsaForwardOrIgnore;

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
IsaForwardOrIgnore(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PISAPNP_COMMON_EXTENSION CommonExt = DeviceObject->DeviceExtension;

    PAGED_CODE();

    DPRINT("%s(%p, %p) Minor - %X\n", __FUNCTION__, DeviceObject, Irp,
           IoGetCurrentIrpStackLocation(Irp)->MinorFunction);

    if (CommonExt->IsFdo)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(((PISAPNP_FDO_EXTENSION)CommonExt)->Ldo, Irp);
    }
    else
    {
        NTSTATUS Status = Irp->IoStatus.Status;

        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
}

CODE_SEG("PAGE")
NTSTATUS
IsaPnpCreateReadPortDORequirements(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _In_opt_ USHORT SelectedPort)
{
    const USHORT Ports[] = { ISAPNP_WRITE_DATA, ISAPNP_ADDRESS,
                             0x274, 0x3E4, 0x204, 0x2E4, 0x354, 0x2F4 };
    ULONG ListSize, i;
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList;
    PIO_RESOURCE_DESCRIPTOR Descriptor;

    PAGED_CODE();

    if (SelectedPort)
    {
        ListSize = sizeof(IO_RESOURCE_REQUIREMENTS_LIST)
                   + (RTL_NUMBER_OF(Ports) + 2) * sizeof(IO_RESOURCE_DESCRIPTOR);
    }
    else
    {
        ListSize = sizeof(IO_RESOURCE_REQUIREMENTS_LIST)
                   + 2 * RTL_NUMBER_OF(Ports) * sizeof(IO_RESOURCE_DESCRIPTOR);
    }
    RequirementsList = ExAllocatePoolZero(PagedPool, ListSize, TAG_ISAPNP);
    if (!RequirementsList)
        return STATUS_NO_MEMORY;

    RequirementsList->ListSize = ListSize;
    RequirementsList->AlternativeLists = 1;

    RequirementsList->List[0].Version = 1;
    RequirementsList->List[0].Revision = 1;
    if (SelectedPort)
        RequirementsList->List[0].Count = RTL_NUMBER_OF(Ports) + 2;
    else
        RequirementsList->List[0].Count = 2 * RTL_NUMBER_OF(Ports);

    if (SelectedPort)
    {
        ULONG j = 0;

        /* ISAPNP_WRITE_DATA and ISAPNP_ADDRESS */
        for (i = 0; i < 2 * 2; i += 2, j += 2)
        {
            Descriptor = &RequirementsList->List[0].Descriptors[j];

            /* Expected port */
            Descriptor[0].Type = CmResourceTypePort;
            Descriptor[0].ShareDisposition = CmResourceShareDeviceExclusive;
            Descriptor[0].Flags = CM_RESOURCE_PORT_16_BIT_DECODE;
            Descriptor[0].u.Port.Length = Ports[i / 2] & 1 ? 0x01 : 0x04;
            Descriptor[0].u.Port.Alignment = 0x01;
            Descriptor[0].u.Port.MinimumAddress.LowPart = Ports[i / 2];
            Descriptor[0].u.Port.MaximumAddress.LowPart = Ports[i / 2] +
                                                          Descriptor[0].u.Port.Length - 1;

            /* ... but mark it as optional */
            Descriptor[1].Option = IO_RESOURCE_ALTERNATIVE;
            Descriptor[1].Type = CmResourceTypePort;
            Descriptor[1].ShareDisposition = CmResourceShareDeviceExclusive;
            Descriptor[1].Flags = CM_RESOURCE_PORT_16_BIT_DECODE;
            Descriptor[1].u.Port.Alignment = 0x01;
        }

        for (i = 2; i < RTL_NUMBER_OF(Ports); i++)
        {
            Descriptor = &RequirementsList->List[0].Descriptors[j++];

            if (Ports[i] != SelectedPort)
                Descriptor->Option = IO_RESOURCE_ALTERNATIVE;
            Descriptor->Type = CmResourceTypePort;
            Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
            Descriptor->Flags = CM_RESOURCE_PORT_16_BIT_DECODE;
            Descriptor->u.Port.Length = Ports[i] & 1 ? 0x01 : 0x04;
            Descriptor->u.Port.Alignment = 0x01;
            Descriptor->u.Port.MinimumAddress.LowPart = Ports[i];
            Descriptor->u.Port.MaximumAddress.LowPart = Ports[i] +
                                                        Descriptor->u.Port.Length - 1;
        }
    }
    else
    {
        for (i = 0; i < 2 * RTL_NUMBER_OF(Ports); i += 2)
        {
            Descriptor = &RequirementsList->List[0].Descriptors[i];

            /* Expected port */
            Descriptor[0].Type = CmResourceTypePort;
            Descriptor[0].ShareDisposition = CmResourceShareDeviceExclusive;
            Descriptor[0].Flags = CM_RESOURCE_PORT_16_BIT_DECODE;
            Descriptor[0].u.Port.Length = Ports[i / 2] & 1 ? 0x01 : 0x04;
            Descriptor[0].u.Port.Alignment = 0x01;
            Descriptor[0].u.Port.MinimumAddress.LowPart = Ports[i / 2];
            Descriptor[0].u.Port.MaximumAddress.LowPart = Ports[i / 2] +
                                                          Descriptor[0].u.Port.Length - 1;

            /* ... but mark it as optional */
            Descriptor[1].Option = IO_RESOURCE_ALTERNATIVE;
            Descriptor[1].Type = CmResourceTypePort;
            Descriptor[1].ShareDisposition = CmResourceShareDeviceExclusive;
            Descriptor[1].Flags = CM_RESOURCE_PORT_16_BIT_DECODE;
            Descriptor[1].u.Port.Alignment = 0x01;
        }
    }

    PdoExt->RequirementsList = RequirementsList;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPnpCreateReadPortDOResources(
    _In_ PISAPNP_PDO_EXTENSION PdoExt)
{
    const USHORT Ports[] = { ISAPNP_WRITE_DATA, ISAPNP_ADDRESS };
    ULONG ListSize, i;
    PCM_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;

    PAGED_CODE();

    ListSize = sizeof(CM_RESOURCE_LIST)
               + (RTL_NUMBER_OF(Ports) - 1) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    ResourceList = ExAllocatePoolZero(PagedPool, ListSize, TAG_ISAPNP);
    if (!ResourceList)
        return STATUS_NO_MEMORY;

    ResourceList->Count = 1;
    ResourceList->List[0].InterfaceType = Internal;
    ResourceList->List[0].PartialResourceList.Version = 1;
    ResourceList->List[0].PartialResourceList.Revision = 1;
    ResourceList->List[0].PartialResourceList.Count = RTL_NUMBER_OF(Ports);

    for (i = 0; i < RTL_NUMBER_OF(Ports); i++)
    {
        Descriptor = &ResourceList->List[0].PartialResourceList.PartialDescriptors[i];
        Descriptor->Type = CmResourceTypePort;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->Flags = CM_RESOURCE_PORT_16_BIT_DECODE;
        Descriptor->u.Port.Length = 0x01;
        Descriptor->u.Port.Start.LowPart = Ports[i];
    }

    PdoExt->ResourceList = ResourceList;
    PdoExt->ResourceListSize = ListSize;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPnpCreateReadPortDO(
    _In_ PISAPNP_FDO_EXTENSION FdoExt)
{
    PISAPNP_PDO_EXTENSION PdoExt;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(ReadPortCreated == FALSE);

    DPRINT("Creating Read Port\n");

    Status = IoCreateDevice(FdoExt->DriverObject,
                            sizeof(ISAPNP_PDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_CONTROLLER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &FdoExt->ReadPortPdo);
    if (!NT_SUCCESS(Status))
        return Status;

    PdoExt = FdoExt->ReadPortPdo->DeviceExtension;
    RtlZeroMemory(PdoExt, sizeof(ISAPNP_PDO_EXTENSION));
    PdoExt->Common.IsFdo = FALSE;
    PdoExt->Common.Self = FdoExt->ReadPortPdo;
    PdoExt->Common.State = dsStopped;
    PdoExt->FdoExt = FdoExt;

    Status = IsaPnpCreateReadPortDORequirements(PdoExt, 0);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = IsaPnpCreateReadPortDOResources(PdoExt);
    if (!NT_SUCCESS(Status))
        goto Failure;

    FdoExt->ReadPortPdo->Flags &= ~DO_DEVICE_INITIALIZING;

    return Status;

Failure:
    IsaPnpRemoveReadPortDO(FdoExt->ReadPortPdo);

    return Status;
}

CODE_SEG("PAGE")
VOID
IsaPnpRemoveReadPortDO(
    _In_ PDEVICE_OBJECT Pdo)
{
    PISAPNP_PDO_EXTENSION ReadPortExt = Pdo->DeviceExtension;

    PAGED_CODE();

    DPRINT("Removing Read Port\n");

    if (ReadPortExt->RequirementsList)
        ExFreePoolWithTag(ReadPortExt->RequirementsList, TAG_ISAPNP);

    if (ReadPortExt->ResourceList)
        ExFreePoolWithTag(ReadPortExt->ResourceList, TAG_ISAPNP);

    IoDeleteDevice(Pdo);
}

CODE_SEG("PAGE")
NTSTATUS
IsaPnpFillDeviceRelations(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _Inout_ PIRP Irp,
    _In_ BOOLEAN IncludeDataPort)
{
    PISAPNP_PDO_EXTENSION PdoExt;
    NTSTATUS Status = STATUS_SUCCESS;
    PLIST_ENTRY CurrentEntry;
    PISAPNP_LOGICAL_DEVICE IsaDevice;
    PDEVICE_RELATIONS DeviceRelations;
    ULONG PdoCount, i = 0;

    PAGED_CODE();

    /* Try to claim the Read Port for our FDO */
    if (!ReadPortCreated)
    {
        Status = IsaPnpCreateReadPortDO(FdoExt);
        if (!NT_SUCCESS(Status))
            return Status;

        ReadPortCreated = TRUE;
    }

    /* Inactive ISA bus */
    if (!FdoExt->ReadPortPdo)
        IncludeDataPort = FALSE;

    IsaPnpAcquireDeviceDataLock(FdoExt);

    PdoCount = FdoExt->DeviceCount;
    if (IncludeDataPort)
        ++PdoCount;

    CurrentEntry = FdoExt->DeviceListHead.Flink;
    while (CurrentEntry != &FdoExt->DeviceListHead)
    {
        IsaDevice = CONTAINING_RECORD(CurrentEntry, ISAPNP_LOGICAL_DEVICE, DeviceLink);

        if (!(IsaDevice->Flags & ISAPNP_PRESENT))
            --PdoCount;

        CurrentEntry = CurrentEntry->Flink;
    }

    DeviceRelations = ExAllocatePoolWithTag(PagedPool,
                                            FIELD_OFFSET(DEVICE_RELATIONS, Objects[PdoCount]),
                                            TAG_ISAPNP);
    if (!DeviceRelations)
    {
        IsaPnpReleaseDeviceDataLock(FdoExt);
        return STATUS_NO_MEMORY;
    }

    if (IncludeDataPort)
    {
        PISAPNP_PDO_EXTENSION ReadPortExt = FdoExt->ReadPortPdo->DeviceExtension;

        DeviceRelations->Objects[i++] = FdoExt->ReadPortPdo;
        ObReferenceObject(FdoExt->ReadPortPdo);

        /* The Read Port PDO can only be removed by FDO */
        ReadPortExt->Flags |= ISAPNP_ENUMERATED;
    }

    CurrentEntry = FdoExt->DeviceListHead.Flink;
    while (CurrentEntry != &FdoExt->DeviceListHead)
    {
        IsaDevice = CONTAINING_RECORD(CurrentEntry, ISAPNP_LOGICAL_DEVICE, DeviceLink);

        if (!(IsaDevice->Flags & ISAPNP_PRESENT))
            goto SkipPdo;

        if (!IsaDevice->Pdo)
        {
            Status = IoCreateDevice(FdoExt->DriverObject,
                                    sizeof(ISAPNP_PDO_EXTENSION),
                                    NULL,
                                    FILE_DEVICE_CONTROLLER,
                                    FILE_DEVICE_SECURE_OPEN | FILE_AUTOGENERATED_DEVICE_NAME,
                                    FALSE,
                                    &IsaDevice->Pdo);
            if (!NT_SUCCESS(Status))
                goto SkipPdo;

            IsaDevice->Pdo->Flags &= ~DO_DEVICE_INITIALIZING;

            PdoExt = IsaDevice->Pdo->DeviceExtension;

            RtlZeroMemory(PdoExt, sizeof(ISAPNP_PDO_EXTENSION));
            PdoExt->Common.IsFdo = FALSE;
            PdoExt->Common.Self = IsaDevice->Pdo;
            PdoExt->Common.State = dsStopped;
            PdoExt->IsaPnpDevice = IsaDevice;
            PdoExt->FdoExt = FdoExt;

            Status = IsaFdoCreateRequirements(PdoExt);
            if (!NT_SUCCESS(Status))
            {
                IsaPnpRemoveLogicalDevice(PdoExt->Common.Self);
                IsaDevice->Pdo = NULL;
                goto SkipPdo;
            }

            Status = IsaFdoCreateResources(PdoExt);
            if (!NT_SUCCESS(Status))
            {
                IsaPnpRemoveLogicalDevice(PdoExt->Common.Self);
                IsaDevice->Pdo = NULL;
                goto SkipPdo;
            }
        }
        else
        {
            PdoExt = IsaDevice->Pdo->DeviceExtension;
        }
        DeviceRelations->Objects[i++] = IsaDevice->Pdo;
        ObReferenceObject(IsaDevice->Pdo);

        PdoExt->Flags |= ISAPNP_ENUMERATED;

        CurrentEntry = CurrentEntry->Flink;
        continue;

SkipPdo:
        PdoExt = IsaDevice->Pdo->DeviceExtension;

        if (PdoExt)
            PdoExt->Flags &= ~ISAPNP_ENUMERATED;

        CurrentEntry = CurrentEntry->Flink;
    }

    IsaPnpReleaseDeviceDataLock(FdoExt);

    DeviceRelations->Count = i;

    Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;

    return Status;
}

static DRIVER_ADD_DEVICE IsaAddDevice;

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
IsaAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_OBJECT Fdo;
    PISAPNP_FDO_EXTENSION FdoExt;
    NTSTATUS Status;
    static ULONG BusNumber = 0;

    PAGED_CODE();

    DPRINT("%s(%p, %p)\n", __FUNCTION__, DriverObject, PhysicalDeviceObject);

    Status = IoCreateDevice(DriverObject,
                            sizeof(*FdoExt),
                            NULL,
                            FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create FDO (0x%x)\n", Status);
        return Status;
    }

    FdoExt = Fdo->DeviceExtension;
    RtlZeroMemory(FdoExt, sizeof(*FdoExt));

    FdoExt->Common.Self = Fdo;
    FdoExt->Common.IsFdo = TRUE;
    FdoExt->Common.State = dsStopped;
    FdoExt->DriverObject = DriverObject;
    FdoExt->BusNumber = BusNumber++;
    FdoExt->Pdo = PhysicalDeviceObject;
    FdoExt->Ldo = IoAttachDeviceToDeviceStack(Fdo,
                                              PhysicalDeviceObject);
    if (!FdoExt->Ldo)
    {
        IoDeleteDevice(Fdo);
        return STATUS_DEVICE_REMOVED;
    }

    InitializeListHead(&FdoExt->DeviceListHead);
    KeInitializeEvent(&FdoExt->DeviceSyncEvent, SynchronizationEvent, TRUE);

    IsaPnpAcquireBusDataLock();
    InsertTailList(&BusListHead, &FdoExt->BusLink);
    IsaPnpReleaseBusDataLock();

    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

static DRIVER_DISPATCH_RAISED IsaPower;

static
NTSTATUS
NTAPI
IsaPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PISAPNP_COMMON_EXTENSION DevExt = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    if (!DevExt->IsFdo)
    {
        Status = Irp->IoStatus.Status;
        PoStartNextPowerIrp(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(((PISAPNP_FDO_EXTENSION)DevExt)->Ldo, Irp);
}

static DRIVER_DISPATCH_PAGED IsaPnp;

static
CODE_SEG("PAGE")
NTSTATUS
NTAPI
IsaPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PISAPNP_COMMON_EXTENSION DevExt = DeviceObject->DeviceExtension;

    PAGED_CODE();

    if (DevExt->IsFdo)
        return IsaFdoPnp((PISAPNP_FDO_EXTENSION)DevExt, Irp, IrpSp);
    else
        return IsaPdoPnp((PISAPNP_PDO_EXTENSION)DevExt, Irp, IrpSp);
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    DPRINT("%s(%p, %wZ)\n", __FUNCTION__, DriverObject, RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_CREATE] = IsaCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = IsaCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IsaForwardOrIgnore;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = IsaForwardOrIgnore;
    DriverObject->MajorFunction[IRP_MJ_PNP] = IsaPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = IsaPower;
    DriverObject->DriverExtension->AddDevice = IsaAddDevice;

    KeInitializeEvent(&BusSyncEvent, SynchronizationEvent, TRUE);
    InitializeListHead(&BusListHead);

    return STATUS_SUCCESS;
}

/* EOF */
