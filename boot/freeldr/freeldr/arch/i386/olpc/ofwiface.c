/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Open Firmware Client Interface
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

/* GLOBALS ********************************************************************/

OFW_IHANDLE OFwpStdInHandle;
OFW_IHANDLE OFwpStdOutHandle;
OFW_PHANDLE OFwpChosenPackageHandle;

/* FUNCTIONS ******************************************************************/

BOOLEAN
OFwFindDevice(
    _In_ PCSTR DeviceSpecifier,
    _Out_ OFW_PHANDLE* Handle)
{
    struct _OFW_FIND_DEVICE
    {
        OFW_CELL ServiceName;
        OFW_CELL ArgumentsCount;
        OFW_CELL ReturnsCount;
        /* In */
        OFW_CELL DeviceSpecifier;
        /* Out */
        OFW_CELL Handle;
    } Arguments;
    OFW_RESULT Result;

    Arguments.ServiceName = MAKE_CELL("finddevice");
    Arguments.ArgumentsCount = 1;
    Arguments.ReturnsCount = 1;
    Arguments.DeviceSpecifier = MAKE_CELL(DeviceSpecifier);

    Result = OFwCallClientInterfaceHandler(&Arguments);
    if (Result == (OFW_RESULT)-1)
        return FALSE;

    if (Arguments.Handle == (OFW_CELL)-1)
        return FALSE;

    *Handle = Arguments.Handle;

    return TRUE;
}

BOOLEAN
OFwInstanceToPackage(
    _In_ OFW_IHANDLE InstanceHandle,
    _Out_ OFW_PHANDLE* PackageHandle)
{
    struct _OFW_INSTANCE_TO_PACKAGE
    {
        OFW_CELL ServiceName;
        OFW_CELL ArgumentsCount;
        OFW_CELL ReturnsCount;
        /* In */
        OFW_CELL InstanceHandle;
        /* Out */
        OFW_CELL PackageHandle;
    } Arguments;
    OFW_RESULT Result;

    Arguments.ServiceName = MAKE_CELL("instance-to-package");
    Arguments.ArgumentsCount = 1;
    Arguments.ReturnsCount = 1;
    Arguments.InstanceHandle = MAKE_CELL(InstanceHandle);

    Result = OFwCallClientInterfaceHandler(&Arguments);
    if (Result == (OFW_RESULT)-1)
        return FALSE;

    if (Arguments.PackageHandle == (OFW_CELL)-1)
        return FALSE;

    *PackageHandle = Arguments.PackageHandle;

    return TRUE;
}

BOOLEAN
OFwClaim(
    _In_ ULONG Address,
    _In_ ULONG Size,
    _In_ ULONG Alignment,
    _Out_opt_ PULONG BaseAddress)
{
    struct _OFW_CLAIM
    {
        OFW_CELL ServiceName;
        OFW_CELL ArgumentsCount;
        OFW_CELL ReturnsCount;
        /* In */
        OFW_CELL Address;
        OFW_CELL Size;
        OFW_CELL Alignment;
        /* Out */
        OFW_CELL BaseAddress;
    } Arguments;
    OFW_RESULT Result;

    Arguments.ServiceName = MAKE_CELL("claim");
    Arguments.ArgumentsCount = 3;
    Arguments.ReturnsCount = 1;
    Arguments.Address = Address;
    Arguments.Size = Size;
    Arguments.Alignment = Alignment;

    Result = OFwCallClientInterfaceHandler(&Arguments);
    if (Result == (OFW_RESULT)-1)
        return FALSE;

    if (BaseAddress)
        *BaseAddress = Arguments.BaseAddress;

    return TRUE;
}

BOOLEAN
OFwGetProperty(
    _In_ OFW_PHANDLE Handle,
    _In_ PCSTR PropertyName,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG PropertySize)
{
    struct _OFW_GET_PROPERTY
    {
        OFW_CELL ServiceName;
        OFW_CELL ArgumentsCount;
        OFW_CELL ReturnsCount;
        /* In */
        OFW_CELL Handle;
        OFW_CELL PropertyName;
        OFW_CELL Buffer;
        OFW_CELL BufferLength;
        /* Out */
        OFW_CELL PropertySize;
    } Arguments;
    OFW_RESULT Result;

    Arguments.ServiceName = MAKE_CELL("getprop");
    Arguments.ArgumentsCount = 4;
    Arguments.ReturnsCount = 1;
    Arguments.Handle = Handle;
    Arguments.PropertyName = MAKE_CELL(PropertyName);
    Arguments.Buffer = MAKE_CELL(Buffer);
    Arguments.BufferLength = BufferLength;

    Result = OFwCallClientInterfaceHandler(&Arguments);
    if (Result == (OFW_RESULT)-1)
        return FALSE;

    if (Arguments.PropertySize == (OFW_CELL)-1)
        return FALSE;

    if (PropertySize)
        *PropertySize = Arguments.PropertySize;

    return TRUE;
}

BOOLEAN
OFwGetPropertyUlong(
    _In_ OFW_PHANDLE Handle,
    _In_ PCSTR PropertyName,
    _Out_ PULONG Value)
{
    ULONG Property;
    BOOLEAN Success;

    Success = OFwGetProperty(Handle, PropertyName, &Property, sizeof(ULONG), NULL);

    *Value = RtlUlongByteSwap(Property);

    return Success;
}

BOOLEAN
OFwOpen(
    _In_ PCSTR DeviceSpecifier,
    _In_ OFW_IHANDLE* Handle)
{
    struct _OFW_OPEN
    {
        OFW_CELL ServiceName;
        OFW_CELL ArgumentsCount;
        OFW_CELL ReturnsCount;
        /* In */
        OFW_CELL DeviceSpecifier;
        /* Out */
        OFW_CELL InstanceHandle;
    } Arguments;
    OFW_RESULT Result;

    Arguments.ServiceName = MAKE_CELL("open");
    Arguments.ArgumentsCount = 1;
    Arguments.ReturnsCount = 1;
    Arguments.DeviceSpecifier = MAKE_CELL(DeviceSpecifier);

    Result = OFwCallClientInterfaceHandler(&Arguments);
    if (Result == (OFW_RESULT)-1)
        return FALSE;

    if (Arguments.InstanceHandle == 0)
        return FALSE;

    *Handle = Arguments.InstanceHandle;

    return TRUE;
}

VOID
OFwClose(
    _In_ OFW_IHANDLE Handle)
{
    struct _OFW_CLOSE
    {
        OFW_CELL ServiceName;
        OFW_CELL ArgumentsCount;
        OFW_CELL ReturnsCount;
        /* In */
        OFW_CELL InstanceHandle;
        /* Out */
    } Arguments;

    Arguments.ServiceName = MAKE_CELL("open");
    Arguments.ArgumentsCount = 1;
    Arguments.ReturnsCount = 0;
    Arguments.InstanceHandle = Handle;

    OFwCallClientInterfaceHandler(&Arguments);
}

BOOLEAN
OFwSeek(
    _In_ OFW_IHANDLE Handle,
    _In_ ULONG PositionLow,
    _In_ ULONG PositionHigh)
{
    struct _OFW_SEEK
    {
        OFW_CELL ServiceName;
        OFW_CELL ArgumentsCount;
        OFW_CELL ReturnsCount;
        /* In */
        OFW_CELL InstanceHandle;
        OFW_CELL PositionHigh;
        OFW_CELL PositionLow;
        /* Out */
        OFW_CELL Status;
    } Arguments;
    OFW_RESULT Result;

    Arguments.ServiceName = MAKE_CELL("seek");
    Arguments.ArgumentsCount = 3;
    Arguments.ReturnsCount = 1;
    Arguments.InstanceHandle = Handle;
    Arguments.PositionHigh = PositionHigh;
    Arguments.PositionLow = PositionLow;

    Result = OFwCallClientInterfaceHandler(&Arguments);
    if (Result == (OFW_RESULT)-1)
        return FALSE;

    if (Arguments.Status == (OFW_CELL)-1)
        return FALSE;

    return TRUE;
}

BOOLEAN
OFwRead(
    _In_ OFW_IHANDLE Handle,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    struct _OFW_READ
    {
        OFW_CELL ServiceName;
        OFW_CELL ArgumentsCount;
        OFW_CELL ReturnsCount;
        /* In */
        OFW_CELL Handle;
        OFW_CELL Address;
        OFW_CELL Length;
        /* Out */
        OFW_CELL Actual;
    } Arguments;
    OFW_RESULT Result;

    Arguments.ServiceName = MAKE_CELL("read");
    Arguments.ArgumentsCount = 3;
    Arguments.ReturnsCount = 1;
    Arguments.Handle = Handle;
    Arguments.Address = MAKE_CELL(Buffer);
    Arguments.Length = Length;

    Result = OFwCallClientInterfaceHandler(&Arguments);
    if (Result == (OFW_RESULT)-1)
        return FALSE;

    if (Arguments.Actual == (OFW_CELL)-1)
        return FALSE;

    return TRUE;
}

BOOLEAN
OFwWrite(
    _In_ OFW_PHANDLE Handle,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    struct _OFW_WRITE
    {
        OFW_CELL ServiceName;
        OFW_CELL ArgumentsCount;
        OFW_CELL ReturnsCount;
        /* In */
        OFW_CELL Handle;
        OFW_CELL Address;
        OFW_CELL Length;
        /* Out */
        OFW_CELL Actual;
    } Arguments;
    OFW_RESULT Result;

    Arguments.ServiceName = MAKE_CELL("write");
    Arguments.ArgumentsCount = 3;
    Arguments.ReturnsCount = 1;
    Arguments.Handle = Handle;
    Arguments.Address = MAKE_CELL(Buffer);
    Arguments.Length = MAKE_CELL(Length);

    Result = OFwCallClientInterfaceHandler(&Arguments);
    if (Result == (OFW_RESULT)-1)
        return FALSE;

    if (Arguments.Actual == (OFW_CELL)-1)
        return FALSE;

    return TRUE;
}

BOOLEAN
OFwInterpret(
    _In_ PCSTR Command,
    _Out_opt_ PULONG ReturnValue)
{
    struct _OFW_INTERPRET
    {
        OFW_CELL ServiceName;
        OFW_CELL ArgumentsCount;
        OFW_CELL ReturnsCount;
        /* In */
        OFW_CELL Command;
        /* Out */
        OFW_CELL CatchResult;
        OFW_CELL ReturnValue;
    } Arguments;
    OFW_RESULT Result;

    Arguments.ServiceName = MAKE_CELL("interpret");
    Arguments.ArgumentsCount = 1;
    Arguments.ReturnsCount = ReturnValue ? 2 : 1;
    Arguments.Command = MAKE_CELL(Command);

    Result = OFwCallClientInterfaceHandler(&Arguments);
    if (Result == (OFW_RESULT)-1)
        return FALSE;

    if (ReturnValue)
        *ReturnValue = Arguments.ReturnValue;

    return TRUE;
}

BOOLEAN
OFwInitialize(VOID)
{
    if (!OFwFindDevice("/chosen", &OFwpChosenPackageHandle))
        return FALSE;

    if (!OFwGetPropertyUlong(OFwpChosenPackageHandle, "stdin", &OFwpStdInHandle))
        return FALSE;

    if (!OFwGetPropertyUlong(OFwpChosenPackageHandle, "stdout", &OFwpStdOutHandle))
        return FALSE;

    return TRUE;
}
