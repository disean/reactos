/*
 * PROJECT:         ReactOS ISA PnP Bus driver
 * PURPOSE:         PDO-specific code
 * COPYRIGHT:       Copyright 2010 Cameron Gutman (cameron.gutman@reactos.org)
 *                  Copyright 2020 Hervé Poussineau (hpoussin@reactos.org)
 *                  Copyright 2021 Dmitry Borisov (di.sean@protonmail.com)
 */

#include <isapnp.h>

#include <initguid.h>
#include <wdmguid.h>

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

    if (PdoExt->SpecialFiles > 0)
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
    PWCHAR Buffer, End;
    size_t CharCount, Remaining;

    PAGED_CODE();

    switch (IrpSp->Parameters.QueryId.IdType)
    {
        case BusQueryDeviceID:
        {
            CharCount = strlen("ISAPNP\\") + 3 + 4 + sizeof(ANSI_NULL);

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
                                           L"ISAPNP\\%.3S%04x",
                                           LogDev->VendorId,
                                           LogDev->ProdId);
            if (!NT_SUCCESS(Status))
                goto Cleanup;

            DPRINT("DeviceID: '%S'\n", Buffer);
            break;
        }

        case BusQueryHardwareIDs:
        {
            PWCHAR IdStart;

            DBG_UNREFERENCED_LOCAL_VARIABLE(IdStart);

            CharCount = strlen("ISAPNP\\") + 3 + 4 + sizeof(ANSI_NULL) +
                        strlen("*") + 3 + 4 + 2 * sizeof(ANSI_NULL);

            Buffer = ExAllocatePoolWithTag(PagedPool,
                                           CharCount * sizeof(WCHAR),
                                           TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            DPRINT("HardwareIDs:\n");

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
                goto Cleanup;

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
                goto Cleanup;

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
                CharCount += strlen("*") + 3 + 4 + sizeof(ANSI_NULL);
            }
            CharCount += sizeof(ANSI_NULL);

            if (CharCount == sizeof(ANSI_NULL))
                return Irp->IoStatus.Status;

            Buffer = ExAllocatePoolWithTag(PagedPool,
                                           CharCount * sizeof(WCHAR),
                                           TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            DPRINT("CompatibleIDs:\n");

            for (Entry = LogDev->CompatibleIdList.Flink, End = Buffer, Remaining = CharCount;
                 Entry != &LogDev->CompatibleIdList;
                 Entry = Entry->Flink)
            {
                PISAPNP_COMPATIBLE_ID_ENTRY CompatibleId =
                    CONTAINING_RECORD(Entry, ISAPNP_COMPATIBLE_ID_ENTRY, IdLink);

                Status = RtlStringCchPrintfExW(End,
                                               Remaining,
                                               &End,
                                               &Remaining,
                                               0,
                                               L"*%.3S%04x",
                                               CompatibleId->VendorId,
                                               CompatibleId->ProdId);
                if (!NT_SUCCESS(Status))
                    goto Cleanup;

                DPRINT("  '%S'\n", Buffer);

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
                goto Cleanup;

            DPRINT("InstanceID: '%S'\n", Buffer);
            break;
        }

        default:
            return Irp->IoStatus.Status;
    }

    Irp->IoStatus.Information = (ULONG_PTR)Buffer;
    return STATUS_SUCCESS;

Cleanup:
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
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PWCHAR Buffer;

    PAGED_CODE();

    switch (IrpSp->Parameters.QueryId.IdType)
    {
        case BusQueryDeviceID:
        {
            static const WCHAR DeviceId[] = L"ISAPNP\\ReadDataPort";

            Buffer = ExAllocatePoolWithTag(PagedPool, sizeof(DeviceId), TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer, DeviceId, sizeof(DeviceId));

            DPRINT("DeviceID: '%S'\n", Buffer);
            break;
        }

        case BusQueryHardwareIDs:
        {
            static const WCHAR HardwareIDs[] = L"ISAPNP\\ReadDataPort\0";

            Buffer = ExAllocatePoolWithTag(PagedPool, sizeof(HardwareIDs), TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer, HardwareIDs, sizeof(HardwareIDs));

            DPRINT("HardwareIDs: '%S'\n", Buffer);
            break;
        }

        case BusQueryCompatibleIDs:
        {
            static const WCHAR CompatibleIDs[] = L"\0";

            Buffer = ExAllocatePoolWithTag(PagedPool, sizeof(CompatibleIDs), TAG_ISAPNP);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer, CompatibleIDs, sizeof(CompatibleIDs));

            DPRINT("CompatibleIDs: '%S'\n", Buffer);
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

            DPRINT("InstanceID: '%S'\n", Buffer);
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
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PISAPNP_FDO_EXTENSION FdoExt = PdoExt->FdoExt;
    PCM_RESOURCE_LIST ResourceList = IrpSp->Parameters.StartDevice.AllocatedResources;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    if (!ResourceList || ResourceList->Count != 1)
    {
        DPRINT1("No resource list (%p) or bad count (%d)\n",
                ResourceList, ResourceList ? ResourceList->Count : 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (ResourceList->List[0].PartialResourceList.Version != 1 ||
        ResourceList->List[0].PartialResourceList.Revision != 1)
    {
        DPRINT1("Bad resource list version (%d.%d)\n",
                ResourceList->List[0].PartialResourceList.Version,
                ResourceList->List[0].PartialResourceList.Revision);
        return STATUS_REVISION_MISMATCH;
    }

    /* Try various Read Ports from the list */
    if (ResourceList->List[0].PartialResourceList.Count > 3)
    {
        for (i = 0; i < ResourceList->List[0].PartialResourceList.Count; i++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor =
                &ResourceList->List[0].PartialResourceList.PartialDescriptors[i];

            if (IS_READ_PORT(PartialDescriptor))
            {
                PUCHAR ReadDataPort = ULongToPtr(PartialDescriptor->u.Port.Start.u.LowPart + 3);

                /* We detected some ISAPNP cards */
                if (IsaHwTryReadDataPort(ReadDataPort) > 0)
                {
                    if (PdoExt->RequirementsList)
                        ExFreePoolWithTag(PdoExt->RequirementsList, TAG_ISAPNP);

                    Status = IsaPnpCreateReadPortDORequirements(PdoExt,
                                                                PartialDescriptor->
                                                                u.Port.Start.u.LowPart);
                    if (!NT_SUCCESS(Status))
                        return Status;

                    PdoExt->Flags |= ISAPNP_READ_PORT_NEED_REBALANCE;

                    IoInvalidateDeviceState(PdoExt->Common.Self);

                    return STATUS_RESOURCE_REQUIREMENTS_CHANGED;
                }
            }
        }
    }
    /* Set the Read Port */
    else if (ResourceList->List[0].PartialResourceList.Count == 3)
    {
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
                    PdoExt->Flags &= ~ISAPNP_READ_PORT_NEED_REBALANCE;

                    FdoExt->ReadDataPort = ReadDataPort;

                    IsaPnpAcquireDeviceDataLock(FdoExt);

                    /* Card identification */
                    Status = IsaHwFillDeviceList(FdoExt);

                    IsaPnpReleaseDeviceDataLock(FdoExt);

                    IoInvalidateDeviceRelations(FdoExt->Pdo, BusRelations);
                    IoInvalidateDeviceRelations(FdoExt->ReadPortPdo, RemovalRelations);

                    return Status;
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
    UNREFERENCED_PARAMETER(Irp);

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

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoRemoveDevice(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp)
{
    PISAPNP_FDO_EXTENSION FdoExt = PdoExt->FdoExt;

    UNREFERENCED_PARAMETER(Irp);

    PAGED_CODE();

    if (!(PdoExt->Flags & ISAPNP_ENUMERATED))
    {
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

    DPRINT("Removing CSN %lu, LDN %lu\n", LogDev->CSN, LogDev->LDN);

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
        DPRINT("%s(%p, %p) CSN %lu, LDN %lu, Minor - %X\n",
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
            if (PdoExt->IsaPnpDevice)
                Status = IsaHwActivateDevice(PdoExt->IsaPnpDevice);
            else
                Status = IsaPdoStartReadPort(PdoExt, IrpSp);

            if (NT_SUCCESS(Status))
                PdoExt->Common.State = dsStarted;
            break;

        case IRP_MN_STOP_DEVICE:
            if (PdoExt->IsaPnpDevice)
                Status = IsaHwDeactivateDevice(PdoExt->IsaPnpDevice);
            else
                Status = STATUS_SUCCESS;

            if (NT_SUCCESS(Status))
                PdoExt->Common.State = dsStopped;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
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
            Status = IsaPdoRemoveDevice(PdoExt, Irp);
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
                Status = IsaReadPortQueryId(PdoExt, Irp, IrpSp);
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
