/*
 * PROJECT:         ReactOS ISA PnP Bus driver
 * PURPOSE:         PDO-specific code
 * COPYRIGHT:       Copyright 2010 Cameron Gutman (cameron.gutman@reactos.org)
 *                  Copyright 2020 Hervé Poussineau (hpoussin@reactos.org)
 */

#include <isapnp.h>

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
    PISAPNP_LOGICAL_DEVICE LogDev = PdoExt->IsaPnpDevice;
    ULONG i;

    PAGED_CODE();

    DeviceCapabilities = IrpSp->Parameters.DeviceCapabilities.Capabilities;
    if (DeviceCapabilities->Version != 1)
        return STATUS_REVISION_MISMATCH;

    if (LogDev)
    {
        DeviceCapabilities->UniqueID = TRUE;
        DeviceCapabilities->Address = LogDev->CSN;
    }
    else
    {
        DeviceCapabilities->UniqueID = FALSE;
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
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PAGED_CODE();

    Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoQueryId(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PUNICODE_STRING Source;
    PWCHAR Buffer;

    PAGED_CODE();

    switch (IrpSp->Parameters.QueryId.IdType)
    {
        case BusQueryDeviceID:
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_ID / BusQueryDeviceID\n");
            Source = &PdoExt->DeviceID;
            break;

        case BusQueryHardwareIDs:
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_ID / BusQueryHardwareIDs\n");
            Source = &PdoExt->HardwareIDs;
            break;

        case BusQueryCompatibleIDs:
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_ID / BusQueryCompatibleIDs\n");
            Source = &PdoExt->CompatibleIDs;
            break;

        case BusQueryInstanceID:
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_ID / BusQueryInstanceID\n");
            Source = &PdoExt->InstanceID;
            break;

        default:
          DPRINT1("IRP_MJ_PNP / IRP_MN_QUERY_ID / unknown query id type 0x%lx\n",
                  IrpSp->Parameters.QueryId.IdType);
          return Irp->IoStatus.Status;
    }

    if (!Source->Buffer)
        return Irp->IoStatus.Status;

    Buffer = ExAllocatePoolWithTag(PagedPool, Source->MaximumLength, TAG_ISAPNP);
    if (!Buffer)
        return STATUS_NO_MEMORY;

    RtlCopyMemory(Buffer, Source->Buffer, Source->MaximumLength);
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

    PAGED_CODE();

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

static
CODE_SEG("PAGE")
NTSTATUS
IsaPdoStartReadPort(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PCM_RESOURCE_LIST ResourceList = IrpSp->Parameters.StartDevice.AllocatedResources;
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
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

    for (i = 0; i < ResourceList->List[0].PartialResourceList.Count; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor =
            &ResourceList->List[0].PartialResourceList.PartialDescriptors[i];

        if (PartialDescriptor->Type == CmResourceTypePort &&
            PartialDescriptor->u.Port.Length > 1 && !FdoExt->ReadDataPort)
        {
            PUCHAR ReadDataPort = ULongToPtr(PartialDescriptor->u.Port.Start.u.LowPart + 3);
            if (NT_SUCCESS(IsaHwTryReadDataPort(ReadDataPort)))
            {
                /* We detected some ISAPNP cards */

                FdoExt->ReadDataPort = ReadDataPort;

                IsaPnpAcquireDeviceDataLock(FdoExt);
                Status = IsaHwFillDeviceList(FdoExt);
                IsaPnpReleaseDeviceDataLock(FdoExt);

                if (FdoExt->DeviceCount > 0)
                {
                    IoInvalidateDeviceRelations(FdoExt->Pdo, BusRelations);
                    IoInvalidateDeviceRelations(FdoExt->ReadPortPdo, RemovalRelations);
                }
            }
            else
            {
                /* Mark read data port as started, even if no card has been detected */
                Status = STATUS_SUCCESS;
            }
        }
    }

    return Status;
}

static
NTSTATUS
NTAPI
IsaPdoOnRepeaterComplete(
    PDEVICE_OBJECT Tdo,
    PIRP SubIrp,
    PVOID NeedsVote)
{
    PIO_STACK_LOCATION SubStack = IoGetCurrentIrpStackLocation(SubIrp);
    PIRP Irp = (PIRP)SubStack->Parameters.Others.Argument1;
    ObDereferenceObject(Tdo);

    if (SubIrp->IoStatus.Status == STATUS_NOT_SUPPORTED)
    {
        if (NeedsVote)
        {
            Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
        }
    }
    else
    {
        Irp->IoStatus = SubIrp->IoStatus;
    }

    IoFreeIrp(SubIrp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NTAPI
IsaPdoRepeatRequest(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _In_ PIRP Irp,
    _In_ BOOLEAN NeedsVote)
{
    PDEVICE_OBJECT Fdo = PdoExt->FdoExt->Common.Self;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT Tdo = IoGetAttachedDeviceReference(Fdo);
    PIRP SubIrp = IoAllocateIrp(Tdo->StackSize + 1, FALSE);
    PIO_STACK_LOCATION SubStack = IoGetNextIrpStackLocation(SubIrp);

    SubStack->DeviceObject = Tdo;
    SubStack->Parameters.Others.Argument1 = (PVOID)Irp;

    IoSetNextIrpStackLocation(SubIrp);
    SubStack = IoGetNextIrpStackLocation(SubIrp);
    RtlCopyMemory(SubStack, Stack, FIELD_OFFSET(IO_STACK_LOCATION, CompletionRoutine));
    SubStack->Control = 0;
    IoSetCompletionRoutine(SubIrp, IsaPdoOnRepeaterComplete, (PVOID)(ULONG_PTR)NeedsVote, TRUE, TRUE, TRUE);

    SubIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoMarkIrpPending(Irp);
    IoCallDriver(Tdo, SubIrp);

    return STATUS_PENDING;
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

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            if (PdoExt->IsaPnpDevice)
                Status = IsaHwActivateDevice(PdoExt->IsaPnpDevice);
            else
                Status = IsaPdoStartReadPort(PdoExt->FdoExt, IrpSp);

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

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            Status = IsaPdoQueryDeviceRelations(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            Status = IsaPdoQueryCapabilities(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            if (PdoExt->Common.Self == PdoExt->FdoExt->ReadPortPdo)
                Status = IsaPdoQueryPnpDeviceState(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = IsaPdoQueryResources(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = IsaPdoQueryResourceRequirements(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_ID:
            Status = IsaPdoQueryId(PdoExt, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_DEVICE_TEXT:
        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_READ_CONFIG:
        case IRP_MN_WRITE_CONFIG:
        case IRP_MN_EJECT:
        case IRP_MN_SET_LOCK:
        case IRP_MN_QUERY_BUS_INFORMATION:
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            return IsaPdoRepeatRequest(PdoExt, Irp, TRUE);

        default:
            DPRINT1("Unknown PnP code: %x\n", IrpSp->MinorFunction);
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}
