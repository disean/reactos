/*
 * PROJECT:     ReactOS IDE Port Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Common header file
 * COPYRIGHT:   Copyright 2020 Dmitry Borisov (di.sean@protonmail.com)
 */

#ifndef _ATAPI_H_
#define _ATAPI_H_

#include <ntddk.h>
#include <ntstrsafe.h>
#include <string.h>
#include <scsi.h>
#include <ntddscsi.h>
#include <ntdddisk.h>
#include <ide.h>
#include <section_attribs.h>
#include <ntddide.h>

#define IDEPORT_TAG 'PedI'

#define IDEPORT_FDO_SIGNATURE 'odFI'
#define IDEPORT_PDO_SIGNATURE 'odPI'

#define IS_FDO(p)    (((PIDEPORT_COMMON_EXTENSION)(p))->Signature == IDEPORT_FDO_SIGNATURE)
#define IS_PDO(p)    (((PIDEPORT_COMMON_EXTENSION)(p))->Signature == IDEPORT_PDO_SIGNATURE)

// #define IDEPORT_SIMULATE_DEVICE_STUCK
// #define IDEPORT_SIMULATE_DEVICE_SURPRISE_REMOVE
// #define IDEPORT_SIMULATE_DEVICE_HOT_SWAP

typedef struct _IDEPORT_COMMON_EXTENSION
{
    ULONG Signature;
    PDEVICE_OBJECT Self;
} IDEPORT_COMMON_EXTENSION, *PIDEPORT_COMMON_EXTENSION;

typedef struct _IDEPORT_CHANNEL_EXTENSION
{
    IDEPORT_COMMON_EXTENSION Common;
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT Ldo;
    PDEVICE_OBJECT Pdo;
} IDEPORT_CHANNEL_EXTENSION, *PIDEPORT_CHANNEL_EXTENSION;

typedef struct _IDEPORT_DEVICE_EXTENSION
{
    IDEPORT_COMMON_EXTENSION Common;
    LIST_ENTRY DeviceLink;
    KSPIN_LOCK SpinLock;
    UNICODE_STRING DeviceID;
    UNICODE_STRING HardwareIDs;
    UNICODE_STRING CompatibleIDs;
    UNICODE_STRING InstanceID;

    _Guarded_by_(SpinLock)
    BOOLEAN Claimed;
} IDEPORT_DEVICE_EXTENSION, *PIDEPORT_DEVICE_EXTENSION;

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define TRACE(fmt, ...) \
    do { \
    if (DbgPrint("ATAPI: (%s:%d) %s " fmt, __FILENAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)) \
        DbgPrint("(%s:%d) DbgPrint() failed!\n", __RELFILE__, __LINE__); \
    } while (0)

#define INFO(fmt, ...) \
    do { \
        if (DbgPrint("ATAPI: (%s:%d) %s " fmt, __FILENAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)) \
            DbgPrint("(%s:%d) DbgPrint() failed!\n", __RELFILE__, __LINE__); \
    } while (0)

#define WARN(fmt, ...) \
    do { \
        if (DbgPrint("ATAPI: (%s:%d) %s " fmt, __FILENAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)) \
            DbgPrint("(%s:%d) DbgPrint() failed!\n", __RELFILE__, __LINE__); \
    } while (0)

#define ERR(fmt, ...) \
    do { \
        if (DbgPrint("ATAPI: (%s:%d) %s " fmt, __FILENAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)) \
            DbgPrint("(%s:%d) DbgPrint() failed!\n", __RELFILE__, __LINE__); \
    } while (0)

#define DPRINT0(fmt, ...) \
    do { \
        if (DbgPrint("ATAPI: (%s:%d) " fmt, __FILENAME__, __LINE__, ##__VA_ARGS__)) \
            DbgPrint("(%s:%d) DbgPrint() failed!\n", __RELFILE__, __LINE__); \
    } while (0)

#endif /* _ATAPI_H_ */
