/*
 * PROJECT:     ReactOS File System Recognizer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     HPFS Recognizer
 * COPYRIGHT:   Copyright 2022 Dmitry Borisov <di.sean@protonmail.com>
 */

/* INCLUDES *******************************************************************/

#include "fs_rec.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

typedef struct _HPFS_SUPER_BLOCK
{
    ULONG Signature1;
    ULONG Signature2;
    UCHAR Unused[504];
} HPFS_SUPER_BLOCK, *PHPFS_SUPER_BLOCK;

typedef struct _HPFS_SPARE_BLOCK
{
    ULONG Signature1;
    ULONG Signature2;
    UCHAR Unused[504];
} HPFS_SPARE_BLOCK, *PHPFS_SPARE_BLOCK;

/* FUNCTIONS ******************************************************************/

static
BOOLEAN
FsRecIsHpfsVolume(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _Out_ PBOOLEAN DeviceError)
{
    LARGE_INTEGER Offset;
    PHPFS_SUPER_BLOCK SuperBlock = NULL;
    PHPFS_SPARE_BLOCK SpareBlock = NULL;
    BOOLEAN Success = FALSE;

    PAGED_CODE();

    /* Read the SuperBlock */
    Offset.QuadPart = 16 * SectorSize;
    if (!FsRecReadBlock(DeviceObject,
                        &Offset,
                        sizeof(HPFS_SUPER_BLOCK),
                        SectorSize,
                        (PVOID*)&SuperBlock,
                        DeviceError))
    {
        goto Cleanup;
    }

    /* Check for magic */
    if (SuperBlock->Signature1 != 0xF995E849 ||
        SuperBlock->Signature2 != 0xFA53E9C5)
    {
        goto Cleanup;
    }

    /* Read the SpareBlock */
    Offset.QuadPart = 17 * SectorSize;
    if (!FsRecReadBlock(DeviceObject,
                        &Offset,
                        sizeof(HPFS_SPARE_BLOCK),
                        SectorSize,
                        (PVOID*)&SpareBlock,
                        DeviceError))
    {
        goto Cleanup;
    }

    /* Check for magic */
    if (SpareBlock->Signature1 != 0xF9911849 ||
        SpareBlock->Signature2 != 0xFA5229C5)
    {
        goto Cleanup;
    }

    Success = TRUE;

Cleanup:
    if (SuperBlock)
        ExFreePool(SuperBlock);
    if (SpareBlock)
        ExFreePool(SpareBlock);

    return Success;
}

NTSTATUS
NTAPI
FsRecHpfsFsControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;

    PAGED_CODE();

    Stack = IoGetCurrentIrpStackLocation(Irp);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_MOUNT_VOLUME:
        {
            ULONG SectorSize;
            PDEVICE_OBJECT MountDevice;
            BOOLEAN DeviceError = FALSE;

            Status = STATUS_UNRECOGNIZED_VOLUME;

            MountDevice = Stack->Parameters.MountVolume.DeviceObject;

            if (FsRecGetDeviceSectorSize(MountDevice, &SectorSize))
            {
                if (FsRecIsHpfsVolume(MountDevice, SectorSize, &DeviceError))
                {
                    Status = STATUS_FS_DRIVER_REQUIRED;
                }
            }
            else
            {
                DeviceError = TRUE;
            }

            if (DeviceError)
            {
                if (MountDevice->Characteristics & FILE_FLOPPY_DISKETTE)
                {
                    Status = STATUS_FS_DRIVER_REQUIRED;
                }
            }

            break;
        }

        case IRP_MN_LOAD_FILE_SYSTEM:
        {
            Status = FsRecLoadFileSystem(DeviceObject,
                                         L"\\Registry\\Machine\\System\\"
                                         L"CurrentControlSet\\Services\\Pinball");
            break;
        }

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    return Status;
}
