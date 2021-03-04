/*
 * PROJECT:         ReactOS ISA PnP Bus driver
 * PURPOSE:         FDO-specific code
 * COPYRIGHT:       Copyright 2010 Cameron Gutman (cameron.gutman@reactos.org)
 *                  Copyright 2020 Hervé Poussineau (hpoussin@reactos.org)
 */

#include <isapnp.h>

#define NDEBUG
#include <debug.h>

static
CODE_SEG("PAGE")
NTSTATUS
IsaFdoStartDevice(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _Inout_ PIRP Irp)
{
    NTSTATUS Status;

    PAGED_CODE();

    if (!IoForwardIrpSynchronously(FdoExt->Ldo, Irp))
    {
        return STATUS_UNSUCCESSFUL;
    }
    Status = Irp->IoStatus.Status;
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    FdoExt->Common.State = dsStarted;

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaFdoQueryDeviceRelations(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    UNREFERENCED_PARAMETER(IrpSp);

    PAGED_CODE();

    return IsaPnpFillDeviceRelations(FdoExt, Irp, TRUE);
}

static
CODE_SEG("PAGE")
NTSTATUS
IsaFdoRemoveDevice(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _Inout_ PIRP Irp)
{
    NTSTATUS Status;
    PLIST_ENTRY CurrentEntry;

    PAGED_CODE();

    IsaPnpAcquireDeviceDataLock(FdoExt);

    /* Remove the logical devices */
    for (CurrentEntry = FdoExt->DeviceListHead.Flink;
         CurrentEntry != &FdoExt->DeviceListHead;
         CurrentEntry = CurrentEntry->Flink)
    {
        PISAPNP_LOGICAL_DEVICE LogDevice = CONTAINING_RECORD(CurrentEntry,
                                                             ISAPNP_LOGICAL_DEVICE,
                                                             DeviceLink);

        RemoveEntryList(&LogDevice->DeviceLink);
        --FdoExt->DeviceCount;

        IsaPnpRemoveLogicalDevice(LogDevice->Pdo);
    }

    /* Remove the Read Port */
    if (FdoExt->ReadPortPdo)
    {
        IsaPnpRemoveReadPortDO(FdoExt->ReadPortPdo->DeviceExtension);

        ReadPortCreated = FALSE;
    }

    IsaPnpReleaseDeviceDataLock(FdoExt);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(FdoExt->Ldo, Irp);

    IsaPnpAcquireBusDataLock();

    /* Find the next ISA bus, if any */
    CurrentEntry = BusListHead.Flink;
    if (CurrentEntry != &BusListHead)
    {
        PISAPNP_FDO_EXTENSION NextIsaBus = CONTAINING_RECORD(CurrentEntry,
                                                             ISAPNP_FDO_EXTENSION,
                                                             BusLink);

        /* Create a new Read Port for it */
        if (!ReadPortCreated)
            IoInvalidateDeviceRelations(NextIsaBus->Pdo, BusRelations);
    }

    RemoveEntryList(&FdoExt->BusLink);

    IsaPnpReleaseBusDataLock();

    IoDetachDevice(FdoExt->Ldo);
    IoDeleteDevice(FdoExt->Common.Self);

    return Status;
}

CODE_SEG("PAGE")
NTSTATUS
IsaFdoPnp(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    NTSTATUS Status = Irp->IoStatus.Status;

    PAGED_CODE();

    DPRINT("%s(%p, %p) FDO %lu, Minor - %X\n",
           __FUNCTION__,
           FdoExt,
           Irp,
           FdoExt->BusNumber,
           IrpSp->MinorFunction);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IsaFdoStartDevice(FdoExt, Irp);

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            if (IrpSp->Parameters.QueryDeviceRelations.Type != BusRelations)
                break;

            Status = IsaFdoQueryDeviceRelations(FdoExt, Irp, IrpSp);
            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);

                return Status;
            }

            Irp->IoStatus.Status = Status;
            break;
        }

        case IRP_MN_REMOVE_DEVICE:
            return IsaFdoRemoveDevice(FdoExt, Irp);

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_STOP_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            DPRINT("Unknown PnP code: %X\n", IrpSp->MinorFunction);
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);

    return IoCallDriver(FdoExt->Ldo, Irp);
}
