/*
 * PROJECT:     ReactOS IDE Port Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PDO functions
 * COPYRIGHT:   Copyright 2020 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include "atapi.h"

// #define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/

NTSTATUS
IdePortPdoDispatchDeviceControl(
    _In_ PIDEPORT_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    INFO("(%p, %p) Dev.%lu IOCTL 0x%lX\n", DevicePdoExtension->Common.Self, Irp,
         0 /* FIXME */, IoStack->Parameters.DeviceIoControl.IoControlCode);

    /* FIXME: Bus remove */

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_SCSI_GET_ADDRESS:
        {
            PSCSI_ADDRESS Address = Irp->AssociatedIrp.SystemBuffer;

            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(SCSI_ADDRESS))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            /* FIXME */
            Address->Length = sizeof(SCSI_ADDRESS);
            Address->PortNumber = 0;
            Address->PathId = 0;
            Address->TargetId = 0;
            Address->Lun = 0;

            Irp->IoStatus.Information = sizeof(SCSI_ADDRESS);
            Status = STATUS_SUCCESS;

            break;
        }
        case IOCTL_STORAGE_QUERY_PROPERTY:
        {
            PSTORAGE_PROPERTY_QUERY PropertyQuery = Irp->AssociatedIrp.SystemBuffer;

            if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(STORAGE_PROPERTY_QUERY))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

#define CHECK_PROPERTY(size)                                         \
            if (PropertyQuery->QueryType != PropertyStandardQuery && \
                PropertyQuery->QueryType != PropertyExistsQuery)     \
            {                                                        \
                Status = STATUS_NOT_SUPPORTED;                       \
                break;                                               \
            }                                                        \
            if (PropertyQuery->QueryType == PropertyExistsQuery)     \
            {                                                        \
                ASSERT(FALSE); /* FIXME */                           \
                Irp->IoStatus.Information = 0;                       \
                Status = STATUS_SUCCESS;                             \
                                                                     \
                break;                                               \
            }                                                        \
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(STORAGE_DESCRIPTOR_HEADER)) \
            {                                                        \
                Status = STATUS_INFO_LENGTH_MISMATCH;                \
                break;                                               \
            }                                                        \
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < size) \
            {                                                        \
                PSTORAGE_DESCRIPTOR_HEADER DescriptorHeader = Irp->AssociatedIrp.SystemBuffer; \
                                                                     \
                DescriptorHeader->Version = size; \
                DescriptorHeader->Size = size; \
                                                                     \
                Irp->IoStatus.Information = sizeof(STORAGE_DESCRIPTOR_HEADER); \
                Status = STATUS_SUCCESS; \
                                                                     \
                break;                                               \
            }
            switch (PropertyQuery->PropertyId)
            {
                case StorageDeviceProperty:
                {
                    PSTORAGE_DEVICE_DESCRIPTOR DeviceDescriptor = Irp->AssociatedIrp.SystemBuffer;
                    ULONG DescriptorSize = sizeof(STORAGE_DEVICE_DESCRIPTOR) + 1; /* FIXME: +1 for NULL */

                    CHECK_PROPERTY(DescriptorSize);

                    DeviceDescriptor->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
                    DeviceDescriptor->Size = DescriptorSize;
                    DeviceDescriptor->DeviceType = 0; /* FIXME */
                    DeviceDescriptor->DeviceTypeModifier = 0; /* FIXME */
                    DeviceDescriptor->RemovableMedia = FALSE; /* FIXME */
                    DeviceDescriptor->CommandQueueing = FALSE;
                    DeviceDescriptor->BusType = BusTypeAtapi;
                    DeviceDescriptor->VendorIdOffset = sizeof(STORAGE_DEVICE_DESCRIPTOR); /* FIXME */
                    DeviceDescriptor->ProductIdOffset = sizeof(STORAGE_DEVICE_DESCRIPTOR); /* FIXME */
                    DeviceDescriptor->ProductRevisionOffset = sizeof(STORAGE_DEVICE_DESCRIPTOR); /* FIXME */
                    DeviceDescriptor->SerialNumberOffset = sizeof(STORAGE_DEVICE_DESCRIPTOR); /* FIXME */

                    Irp->IoStatus.Information = DescriptorSize;
                    Status = STATUS_SUCCESS;

                    break;
                }

                case StorageAdapterProperty:
                {
                    PSTORAGE_ADAPTER_DESCRIPTOR AdapterDescriptor = Irp->AssociatedIrp.SystemBuffer;

                    CHECK_PROPERTY(sizeof(STORAGE_ADAPTER_DESCRIPTOR));

                    AdapterDescriptor->Version = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
                    AdapterDescriptor->Size = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
                    AdapterDescriptor->MaximumTransferLength = 0x20000; /* FIXME: UDMA buffer boundary */
                    AdapterDescriptor->MaximumPhysicalPages = 0xFFFFFFFF; /* FIXME */
                    AdapterDescriptor->AlignmentMask = DevicePdoExtension->Common.Self->AlignmentRequirement;
                    AdapterDescriptor->AdapterUsesPio = TRUE;
                    AdapterDescriptor->AdapterScansDown = FALSE;
                    AdapterDescriptor->CommandQueueing = FALSE;
                    AdapterDescriptor->AcceleratedTransfer = FALSE;
                    AdapterDescriptor->BusType = BusTypeAtapi;
                    AdapterDescriptor->BusMajorVersion = 1;
                    AdapterDescriptor->BusMinorVersion = 0;

                    Irp->IoStatus.Information = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
                    Status = STATUS_SUCCESS;

                    break;
                }

                /* FIXME: Support "modern" properties */
                default:
                    break;
            }
#undef CHECK_PROPERTY
        }

        default:
            // Status = STATUS_INVALID_DEVICE_REQUEST;
            Status = Irp->IoStatus.Status;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}
