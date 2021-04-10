/*
 * PROJECT:         ReactOS ISA PnP Bus driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         PDO-specific code
 * COPYRIGHT:       Copyright 2010 Cameron Gutman (cameron.gutman@reactos.org)
 *                  Copyright 2020 Hervé Poussineau (hpoussin@reactos.org)
 *                  Copyright 2021 Dmitry Borisov (di.sean@protonmail.com)
 */

#include "isapnp.h"

#define NDEBUG
#include <debug.h>

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoQueryDeviceRelations(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PDEVICE_RELATIONS DeviceRelations;

    PAGED_CODE();

    if (IrpSp->Parameters.QueryDeviceRelations.Type == RemovalRelations &&
        PdoExt->Common.Self == PdoExt->FdoExt->ReadPortPdo)
    {
        return IsaPnpFillDeviceRelations(PdoExt->FdoExt, Irp, FALSE);
    }

    if (IrpSp->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation)
        return Irp->IoStatus.Status;

    DeviceRelations = ExAllocatePoolWithTag(PagedPool, sizeof(*DeviceRelations), TAG_ISAPNP);
    if (!DeviceRelations)
        return STATUS_NO_MEMORY;

    DeviceRelations->Count = 1;
    DeviceRelations->Objects[0] = PdoExt->Common.Self;
    ObReferenceObject(PdoExt->Common.Self);

    Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoQueryCapabilities(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PDEVICE_CAPABILITIES DeviceCapabilities;
    ULONG i;

    UNREFERENCED_PARAMETER(Irp);

    PAGED_CODE();

    DeviceCapabilities = IrpSp->Parameters.DeviceCapabilities.Capabilities;
    if (DeviceCapabilities->Version != 1)
        return STATUS_REVISION_MISMATCH;

    DeviceCapabilities->UniqueID = TRUE;

    if (PdoExt->FdoExt->ReadPortPdo &&
        PdoExt->Common.Self == PdoExt->FdoExt->ReadPortPdo)
    {
        DeviceCapabilities->RawDeviceOK = TRUE;
        DeviceCapabilities->SilentInstall = TRUE;
    }

    for (i = 0; i < POWER_SYSTEM_MAXIMUM; i++)
        DeviceCapabilities->DeviceState[i] = PowerDeviceD3;
    DeviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoQueryPnpDeviceState(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp)
{
    PAGED_CODE();

    if (PdoExt->Flags & ISAPNP_READ_PORT_NEED_REBALANCE)
    {
        Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE |
                                     PNP_DEVICE_RESOURCE_REQUIREMENTS_CHANGED |
                                     PNP_DEVICE_FAILED;
        return STATUS_SUCCESS;
    }
    else if (PdoExt->SpecialFiles > 0)
    {
        Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;
        return STATUS_SUCCESS;
    }

    return Irp->IoStatus.Status;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoQueryId(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PISAPNP_LOGICAL_DEVICE LogDev = PdoExt->IsaPnpDevice;
    NTSTATUS Status;
    PWCHAR Buffer, End, IdStart;
    size_t CharCount, Remaining;

    PAGED_CODE();

    switch (IrpSp->Parameters.QueryId.IdType)
    {
        case BusQueryDeviceID:
        {
            CharCount = strlen("ISAPNP\\XXXFFFF") + sizeof(ANSI_NULL);

            if (LogDev->Flags & ISAPNP_HAS_MULTIPLE_LOGDEVS)
                CharCount += strlen("_DEV0000");

            Buffer = ExAllocatePoolWithTag(PagedPool,
                                           CharCount * sizeof(WCHAR),
                                           TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            Status = RtlStringCchPrintfExW(Buffer,
                                           CharCount,
                                           &End,
                                           &Remaining,
                                           0,
                                           L"ISAPNP\\%.3S%04x",
                                           LogDev->VendorId,
                                           LogDev->ProdId);
            if (!NT_SUCCESS(Status))
                goto Failure;

            if (LogDev->Flags & ISAPNP_HAS_MULTIPLE_LOGDEVS)
            {
                Status = RtlStringCchPrintfExW(End,
                                               Remaining,
                                               NULL,
                                               NULL,
                                               0,
                                               L"_DEV%04X",
                                               LogDev->LDN);
                if (!NT_SUCCESS(Status))
                    goto Failure;
            }

            DPRINT("Device ID: '%S'\n", Buffer);
            break;
        }

        case BusQueryHardwareIDs:
        {
            CharCount = strlen("ISAPNP\\XXXFFFF") + sizeof(ANSI_NULL) +
                        strlen("*PNPxxxx") + sizeof(ANSI_NULL) +
                        sizeof(ANSI_NULL); /* multi-string */

            if (LogDev->Flags & ISAPNP_HAS_MULTIPLE_LOGDEVS)
                CharCount += strlen("_DEV0000");

            Buffer = ExAllocatePoolWithTag(PagedPool,
                                           CharCount * sizeof(WCHAR),
                                           TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            DPRINT("Hardware IDs:\n");

            /* 1 */
            Status = RtlStringCchPrintfExW(Buffer,
                                           CharCount,
                                           &End,
                                           &Remaining,
                                           0,
                                           L"ISAPNP\\%.3S%04x",
                                           LogDev->VendorId,
                                           LogDev->ProdId);
            if (!NT_SUCCESS(Status))
                goto Failure;

            if (LogDev->Flags & ISAPNP_HAS_MULTIPLE_LOGDEVS)
            {
                Status = RtlStringCchPrintfExW(End,
                                               Remaining,
                                               &End,
                                               &Remaining,
                                               0,
                                               L"_DEV%04X",
                                               LogDev->LDN);
                if (!NT_SUCCESS(Status))
                    goto Failure;
            }

            DPRINT("  '%S'\n", Buffer);

            ++End;
            --Remaining;

            /* 2 */
            IdStart = End;
            Status = RtlStringCchPrintfExW(End,
                                           Remaining,
                                           &End,
                                           &Remaining,
                                           0,
                                           L"*%.3S%04x",
                                           LogDev->LogVendorId,
                                           LogDev->LogProdId);
            if (!NT_SUCCESS(Status))
                goto Failure;

            DPRINT("  '%S'\n", IdStart);

            *++End = UNICODE_NULL;
            --Remaining;

            break;
        }

        case BusQueryCompatibleIDs:
        {
            PLIST_ENTRY Entry;

            for (Entry = LogDev->CompatibleIdList.Flink, CharCount = 0;
                 Entry != &LogDev->CompatibleIdList;
                 Entry = Entry->Flink)
            {
                CharCount += strlen("*PNPxxxx") + sizeof(ANSI_NULL);
            }
            CharCount += sizeof(ANSI_NULL); /* multi-string */

            if (CharCount == sizeof(ANSI_NULL))
                return Irp->IoStatus.Status;

            Buffer = ExAllocatePoolWithTag(PagedPool,
                                           CharCount * sizeof(WCHAR),
                                           TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            DPRINT("Compatible IDs:\n");

            for (Entry = LogDev->CompatibleIdList.Flink, End = Buffer, Remaining = CharCount;
                 Entry != &LogDev->CompatibleIdList;
                 Entry = Entry->Flink)
            {
                PISAPNP_COMPATIBLE_ID_ENTRY CompatibleId =
                    CONTAINING_RECORD(Entry, ISAPNP_COMPATIBLE_ID_ENTRY, IdLink);

                IdStart = End;
                Status = RtlStringCchPrintfExW(End,
                                               Remaining,
                                               &End,
                                               &Remaining,
                                               0,
                                               L"*%.3S%04x",
                                               CompatibleId->VendorId,
                                               CompatibleId->ProdId);
                if (!NT_SUCCESS(Status))
                    goto Failure;

                DPRINT("  '%S'\n", IdStart);

                ++End;
                --Remaining;
            }

            *End = UNICODE_NULL;

            break;
        }

        case BusQueryInstanceID:
        {
            CharCount = sizeof(LogDev->SerialNumber) * 2 + sizeof(ANSI_NULL);

            Buffer = ExAllocatePoolWithTag(PagedPool,
                                           CharCount * sizeof(WCHAR),
                                           TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            Status = RtlStringCchPrintfExW(Buffer,
                                           CharCount,
                                           NULL,
                                           NULL,
                                           0,
                                           L"%X",
                                           LogDev->SerialNumber);
            if (!NT_SUCCESS(Status))
                goto Failure;

            DPRINT("Instance ID: '%S'\n", Buffer);
            break;
        }

        default:
            return Irp->IoStatus.Status;
    }

    Irp->IoStatus.Information = (ULONG_PTR)Buffer;
    return STATUS_SUCCESS;

Failure:
    /* This should never happen */
    ASSERT(FALSE);

    if (Buffer)
        ExFreePoolWithTag(Buffer, TAG_ISAPNP);

    return Status;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaReadPortQueryId(
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PWCHAR Buffer;
    static const WCHAR ReadPortId[] = L"ISAPNP\\ReadDataPort";

    PAGED_CODE();

    switch (IrpSp->Parameters.QueryId.IdType)
    {
        case BusQueryDeviceID:
        {
            Buffer = ExAllocatePoolWithTag(PagedPool, sizeof(ReadPortId), TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer, ReadPortId, sizeof(ReadPortId));

            DPRINT("Device ID: '%S'\n", Buffer);
            break;
        }

        case BusQueryHardwareIDs:
        {
            Buffer = ExAllocatePoolWithTag(PagedPool,
                                           sizeof(ReadPortId) + sizeof(UNICODE_NULL),
                                           TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer, ReadPortId, sizeof(ReadPortId));

            Buffer[sizeof(ReadPortId) / sizeof(WCHAR)] = UNICODE_NULL; /* multi-string */

            DPRINT("Hardware ID: '%S'\n", Buffer);
            break;
        }

        case BusQueryCompatibleIDs:
        {
            /* Empty multi-string */
            Buffer = ExAllocatePoolZero(PagedPool, sizeof(UNICODE_NULL), TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            DPRINT("Compatible ID: '%S'\n", Buffer);
            break;
        }

        case BusQueryInstanceID:
        {
            /* Even if there are multiple ISA buses, the driver has only one Read Port */
            static const WCHAR InstanceId[] = L"0";

            Buffer = ExAllocatePoolWithTag(PagedPool, sizeof(InstanceId), TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer, InstanceId, sizeof(InstanceId));

            DPRINT("Instance ID: '%S'\n", Buffer);
            break;
        }

        default:
            return Irp->IoStatus.Status;
    }

    Irp->IoStatus.Information = (ULONG_PTR)Buffer;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoQueryDeviceText(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    NTSTATUS Status;
    PWCHAR Buffer;
    size_t CharCount;

    PAGED_CODE();

    switch (IrpSp->Parameters.QueryDeviceText.DeviceTextType)
    {
        case DeviceTextDescription:
        {
            if (!PdoExt->IsaPnpDevice->FriendlyName)
                return Irp->IoStatus.Status;

            CharCount = strlen(PdoExt->IsaPnpDevice->FriendlyName) +
                        sizeof(ANSI_NULL);

            if (CharCount == sizeof(ANSI_NULL))
                return Irp->IoStatus.Status;

            Buffer = ExAllocatePoolWithTag(PagedPool,
                                           CharCount * sizeof(WCHAR),
                                           TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            Status = RtlStringCchPrintfExW(Buffer,
                                           CharCount,
                                           NULL,
                                           NULL,
                                           0,
                                           L"%hs",
                                           PdoExt->IsaPnpDevice->FriendlyName);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Buffer, TAG_ISAPNP);
                return Status;
            }

            DPRINT("TextDescription: '%S'\n", Buffer);
            break;
        }

        default:
            return Irp->IoStatus.Status;
    }

    Irp->IoStatus.Information = (ULONG_PTR)Buffer;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoQueryResources(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    ULONG ListSize;
    PCM_RESOURCE_LIST ResourceList;

    UNREFERENCED_PARAMETER(IrpSp);

    PAGED_CODE();

    if (PdoExt->IsaPnpDevice &&
        !(PdoExt->IsaPnpDevice->Flags & ISAPNP_HAS_RESOURCES))
    {
        Irp->IoStatus.Information = 0;
        return STATUS_SUCCESS;
    }

    if (!PdoExt->ResourceList)
        return Irp->IoStatus.Status;

    ListSize = PdoExt->ResourceListSize;
    ResourceList = ExAllocatePoolWithTag(PagedPool, ListSize, TAG_ISAPNP);
    if (!ResourceList)
        return STATUS_NO_MEMORY;

    RtlCopyMemory(ResourceList, PdoExt->ResourceList, ListSize);
    Irp->IoStatus.Information = (ULONG_PTR)ResourceList;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoQueryResourceRequirements(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    ULONG ListSize;
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList;

    UNREFERENCED_PARAMETER(IrpSp);

    PAGED_CODE();

    if (!PdoExt->RequirementsList)
        return Irp->IoStatus.Status;

    ListSize = PdoExt->RequirementsList->ListSize;
    RequirementsList = ExAllocatePoolWithTag(PagedPool, ListSize, TAG_ISAPNP);
    if (!RequirementsList)
        return STATUS_NO_MEMORY;

    RtlCopyMemory(RequirementsList, PdoExt->RequirementsList, ListSize);
    Irp->IoStatus.Information = (ULONG_PTR)RequirementsList;
    return STATUS_SUCCESS;
}

#define IS_READ_PORT(_d) ((_d)->Type == CmResourceTypePort && (_d)->u.Port.Length > 1)

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoStartReadPort(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _In_ PCM_RESOURCE_LIST ResourceList)
{
    PISAPNP_FDO_EXTENSION FdoExt = PdoExt->FdoExt;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    if (!ResourceList)
    {
        DPRINT1("No resource list\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (ResourceList->List[0].PartialResourceList.Version != 1 ||
        ResourceList->List[0].PartialResourceList.Revision != 1)
    {
        DPRINT1("Bad resource list version (%u.%u)\n",
                ResourceList->List[0].PartialResourceList.Version,
                ResourceList->List[0].PartialResourceList.Revision);
        return STATUS_REVISION_MISMATCH;
    }

#if 0
    /* Try various Read Ports from the list */
    if (ResourceList->List[0].PartialResourceList.Count > 3)
    {
        ULONG SelectedPort = 0;

        for (i = 0; i < ResourceList->List[0].PartialResourceList.Count; i++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor =
                &ResourceList->List[0].PartialResourceList.PartialDescriptors[i];

            if (IS_READ_PORT(PartialDescriptor))
            {
                PUCHAR ReadDataPort = ULongToPtr(PartialDescriptor->u.Port.Start.u.LowPart + 3);
                ULONG Cards;

                /*
                 * Remember the first Read Port in the resource list.
                 * It will be selected by default even if no card has been detected.
                 */
                if (!SelectedPort)
                    SelectedPort = PartialDescriptor->u.Port.Start.u.LowPart;

                Cards = IsaHwTryReadDataPort(ReadDataPort);
                IsaHwWaitForKey();

                /* We detected some ISAPNP cards */
                if (Cards > 0)
                {
                    SelectedPort = PartialDescriptor->u.Port.Start.u.LowPart;
                    break;
                }
            }
        }

        ASSERT(SelectedPort != 0);

        if (PdoExt->RequirementsList)
            ExFreePoolWithTag(PdoExt->RequirementsList, TAG_ISAPNP);

        /* Discard the Read Ports at conflicting locations */
        Status = IsaPnpCreateReadPortDORequirements(PdoExt, SelectedPort);
        if (!NT_SUCCESS(Status))
            return Status;

        PdoExt->Flags |= ISAPNP_READ_PORT_NEED_REBALANCE;

        IoInvalidateDeviceState(PdoExt->Common.Self);

        return STATUS_SUCCESS;
    }
    /* Set the Read Port */
    else if (ResourceList->List[0].PartialResourceList.Count == 3)
#else
    if (ResourceList->List[0].PartialResourceList.Count > 3) /* Temporary HACK */
#endif
    {
        PdoExt->Flags &= ~ISAPNP_READ_PORT_NEED_REBALANCE;

        for (i = 0; i < ResourceList->List[0].PartialResourceList.Count; i++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor =
                &ResourceList->List[0].PartialResourceList.PartialDescriptors[i];

            if (IS_READ_PORT(PartialDescriptor))
            {
                PUCHAR ReadDataPort = ULongToPtr(PartialDescriptor->u.Port.Start.u.LowPart + 3);

                /* Run the isolation protocol */
                FdoExt->Cards = IsaHwTryReadDataPort(ReadDataPort);

                if (FdoExt->Cards > 0)
                {
                    FdoExt->ReadDataPort = ReadDataPort;

                    IsaPnpAcquireDeviceDataLock(FdoExt);

                    /* Card identification */
                    Status = IsaHwFillDeviceList(FdoExt);
                    IsaHwWaitForKey();

                    IsaPnpReleaseDeviceDataLock(FdoExt);

                    PdoExt->Flags |= ISAPNP_READ_PORT_ALLOW_FDO_SCAN |
                                     ISAPNP_SCANNED_BY_READ_PORT;

                    IoInvalidateDeviceRelations(FdoExt->Pdo, BusRelations);
                    IoInvalidateDeviceRelations(FdoExt->ReadPortPdo, RemovalRelations);

                    return Status;
                }
                else
                {
                    IsaHwWaitForKey();
                    break;
                }
            }
        }
    }
    else
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* Mark Read Port as started, even if no card has been detected */
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoFilterResourceRequirements(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PAGED_CODE();

    /* TODO: Handle */
    UNREFERENCED_PARAMETER(PdoExt);
    UNREFERENCED_PARAMETER(IrpSp);
    return Irp->IoStatus.Status;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoQueryBusInformation(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp)
{
    PPNP_BUS_INFORMATION BusInformation;

    PAGED_CODE();

    BusInformation = ExAllocatePoolWithTag(PagedPool,
                                           sizeof(PNP_BUS_INFORMATION),
                                           TAG_ISAPNP);
    if (!BusInformation)
        return STATUS_INSUFFICIENT_RESOURCES;

    BusInformation->BusTypeGuid = GUID_BUS_TYPE_ISAPNP;
    BusInformation->LegacyBusType = Isa;
    BusInformation->BusNumber = PdoExt->FdoExt->BusNumber;

    Irp->IoStatus.Information = (ULONG_PTR)BusInformation;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoQueryDeviceUsageNotification(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    BOOLEAN InPath = IrpSp->Parameters.UsageNotification.InPath;

    PAGED_CODE();

    switch (IrpSp->Parameters.UsageNotification.Type)
    {
        case DeviceUsageTypePaging:
        case DeviceUsageTypeHibernation:
        case DeviceUsageTypeDumpFile:
            IoAdjustPagingPathCount(&PdoExt->SpecialFiles, InPath);
            IoInvalidateDeviceState(PdoExt->Common.Self);
            break;

        default:
            return Irp->IoStatus.Status;
    }

    /* Do not send it to FDO for compatibility */
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoRemoveDevice(
    _In_ PISAPNP_PDO_EXTENSION PdoExt)
{
    PAGED_CODE();

    if (!(PdoExt->Flags & ISAPNP_ENUMERATED))
    {
        PISAPNP_FDO_EXTENSION FdoExt = PdoExt->FdoExt;

        if (PdoExt->IsaPnpDevice && FdoExt)
        {
            IsaPnpAcquireDeviceDataLock(FdoExt);

            RemoveEntryList(&PdoExt->IsaPnpDevice->DeviceLink);
            --FdoExt->DeviceCount;

            IsaPnpReleaseDeviceDataLock(FdoExt);
        }

        if (PdoExt->IsaPnpDevice)
            IsaPnpRemoveLogicalDevice(PdoExt->Common.Self);
        else
            IsaPnpRemoveReadPortDO(PdoExt->Common.Self);
    }

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
VOID
IsaPnpRemoveLogicalDevice(
    _In_ PDEVICE_OBJECT Pdo)
{
    PISAPNP_PDO_EXTENSION PdoExt = Pdo->DeviceExtension;
    PISAPNP_LOGICAL_DEVICE LogDev = PdoExt->IsaPnpDevice;
    PLIST_ENTRY Entry;

    PAGED_CODE();
    ASSERT(LogDev);

    DPRINT("Removing CSN %u, LDN %u\n", LogDev->CSN, LogDev->LDN);

    /* Power down the device */
    if (PdoExt->Common.State == dsStarted)
    {
        IsaHwWakeDevice(LogDev);
        IsaHwDeactivateDevice(LogDev);

        IsaHwWaitForKey();
    }

    if (PdoExt->RequirementsList)
        ExFreePoolWithTag(PdoExt->RequirementsList, TAG_ISAPNP);

    if (PdoExt->ResourceList)
        ExFreePoolWithTag(PdoExt->ResourceList, TAG_ISAPNP);

    if (LogDev->FriendlyName)
        ExFreePoolWithTag(LogDev->FriendlyName, TAG_ISAPNP);

    if (LogDev->Alternatives)
        ExFreePoolWithTag(LogDev->Alternatives, TAG_ISAPNP);

    for (Entry = LogDev->CompatibleIdList.Flink;
         Entry != &LogDev->CompatibleIdList;
         Entry = Entry->Flink)
    {
        PISAPNP_COMPATIBLE_ID_ENTRY CompatibleId =
            CONTAINING_RECORD(Entry, ISAPNP_COMPATIBLE_ID_ENTRY, IdLink);

        RemoveEntryList(&CompatibleId->IdLink);
        ExFreePoolWithTag(CompatibleId, TAG_ISAPNP);
    }

    IoDeleteDevice(PdoExt->Common.Self);
}

CODE_SEG("PAGE")
NTSTATUS
IsaPdoPnp(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    NTSTATUS Status = Irp->IoStatus.Status;

    PAGED_CODE();

    if (PdoExt->IsaPnpDevice)
    {
        DPRINT("%s(%p, %p) CSN %u, LDN %u, Minor - %X\n",
               __FUNCTION__,
               PdoExt,
               Irp,
               PdoExt->IsaPnpDevice->CSN,
               PdoExt->IsaPnpDevice->LDN,
               IrpSp->MinorFunction);
    }
    else
    {
        DPRINT("%s(%p, %p) ReadPort, Minor - %X\n",
               __FUNCTION__,
               PdoExt,
               Irp,
               IrpSp->MinorFunction);
    }

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            if (PdoExt->IsaPnpDevice)
            {
                IsaHwWakeDevice(PdoExt->IsaPnpDevice);

                Status = IsaHwConfigureDevice(PdoExt->FdoExt,
                                              PdoExt->IsaPnpDevice,
                                              IrpSp->Parameters.StartDevice.AllocatedResources);
                if (NT_SUCCESS(Status))
                {
                    IsaHwActivateDevice(PdoExt->FdoExt, PdoExt->IsaPnpDevice);
                }
                else
                {
                    DPRINT1("Failed to configure CSN %u, LDN %u with status 0x%08lx\n",
                            PdoExt->IsaPnpDevice->CSN, PdoExt->IsaPnpDevice->LDN, Status);
                }

                IsaHwWaitForKey();
            }
            else
            {
                Status = IsaPdoStartReadPort(PdoExt,
                                             IrpSp->Parameters.StartDevice.AllocatedResources);
            }

            if (NT_SUCCESS(Status))
                PdoExt->Common.State = dsStarted;
            break;
        }

        case IRP_MN_STOP_DEVICE:
        {
            if (PdoExt->IsaPnpDevice)
            {
                IsaHwWakeDevice(PdoExt->IsaPnpDevice);
                IsaHwDeactivateDevice(PdoExt->IsaPnpDevice);

                IsaHwWaitForKey();
            }
            else
            {
                PdoExt->Flags &= ~ISAPNP_READ_PORT_ALLOW_FDO_SCAN;
            }

            Status = STATUS_SUCCESS;

            if (NT_SUCCESS(Status))
                PdoExt->Common.State = dsStopped;
            break;
        }

        case IRP_MN_QUERY_STOP_DEVICE:
        {
            if (PdoExt->SpecialFiles > 0)
                Status = STATUS_DEVICE_BUSY;
            else if (PdoExt->Flags & ISAPNP_READ_PORT_NEED_REBALANCE)
                Status = STATUS_RESOURCE_REQUIREMENTS_CHANGED;
            else
                Status = STATUS_SUCCESS;

            break;
        }

        case IRP_MN_QUERY_REMOVE_DEVICE:
        {
            if (PdoExt->SpecialFiles > 0)
                Status = STATUS_DEVICE_BUSY;
            else
                Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            Status = IsaPdoQueryDeviceRelations(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            Status = IsaPdoQueryCapabilities(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = IsaPdoRemoveDevice(PdoExt);
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            Status = IsaPdoQueryPnpDeviceState(PdoExt, Irp);
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = IsaPdoQueryResources(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = IsaPdoQueryResourceRequirements(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_ID:
            if (PdoExt->IsaPnpDevice)
                Status = IsaPdoQueryId(PdoExt, Irp, IrpSp);
            else
                Status = IsaReadPortQueryId(Irp, IrpSp);
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (PdoExt->IsaPnpDevice)
                Status = IsaPdoQueryDeviceText(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            Status = IsaPdoFilterResourceRequirements(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
            Status = IsaPdoQueryBusInformation(PdoExt, Irp);
            break;

        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            Status = IsaPdoQueryDeviceUsageNotification(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        default:
            DPRINT("Unknown PnP code: %X\n", IrpSp->MinorFunction);
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}
