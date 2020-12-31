/*
 * PROJECT:     ReactOS IDE Port Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver entrypoint
 * COPYRIGHT:   Copyright 2020 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include "atapi.h"

// #define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/



/* FUNCTIONS ******************************************************************/

CODE_SEG("PAGE")
NTSTATUS
NTAPI
IdePortCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAGED_CODE();

    INFO("(%p, %p) %X\n", DeviceObject, Irp,
         IoGetCurrentIrpStackLocation(Irp)->MajorFunction);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    INFO("#(%p, %wZ)\n", DriverObject, RegistryPath);

#if 0
    DriverObject->MajorFunction[IRP_MJ_CREATE] =
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = IdePortCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IdePortDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_SCSI] = IdePortDispatchScsi;
    DriverObject->MajorFunction[IRP_MJ_PNP] = IdePortDispatchPnp;
    DriverObject->DriverExtension->AddDevice = IdePortAddChannelFdo;
    DriverObject->DriverUnload = IdePortUnload;
#endif

    return STATUS_SUCCESS;
}
