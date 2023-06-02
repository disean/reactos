/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Open Firmware header file
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov (di.sean@protonmail.com)
 */

#pragma once

typedef ULONG OFW_PHANDLE;
typedef ULONG OFW_IHANDLE;
typedef ULONG OFW_CELL;
typedef ULONG OFW_RESULT;

#define MAKE_CELL(x)    ((OFW_CELL)x)

extern OFW_IHANDLE OFwpStdInHandle;
extern OFW_IHANDLE OFwpStdOutHandle;
extern OFW_PHANDLE OFwpChosenPackageHandle;

OFW_RESULT
NTAPI
OFwCallClientInterfaceHandler(
    _In_ PVOID Arguments);

BOOLEAN
OFwInitialize(VOID);

BOOLEAN
OFwVideoInit(VOID);

BOOLEAN
OFwFindDevice(
    _In_ PCSTR DeviceSpecifier,
    _Out_ OFW_PHANDLE* Handle);

BOOLEAN
OFwInstanceToPackage(
    _In_ OFW_IHANDLE InstanceHandle,
    _Out_ OFW_PHANDLE* PackageHandle);

BOOLEAN
OFwClaim(
    _In_ ULONG Address,
    _In_ ULONG Size,
    _In_ ULONG Alignment,
    _Out_opt_ PULONG BaseAddress);

BOOLEAN
OFwGetProperty(
    _In_ OFW_PHANDLE Handle,
    _In_ PCSTR PropertyName,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG PropertySize);

BOOLEAN
OFwGetPropertyUlong(
    _In_ OFW_PHANDLE Handle,
    _In_ PCSTR PropertyName,
    _Out_ PULONG Value);

BOOLEAN
OFwOpen(
    _In_ PCSTR DeviceSpecifier,
    _In_ OFW_IHANDLE* Handle);

VOID
OFwClose(
    _In_ OFW_IHANDLE Handle);

BOOLEAN
OFwSeek(
    _In_ OFW_IHANDLE Handle,
    _In_ ULONG PositionLow,
    _In_ ULONG PositionHigh);

BOOLEAN
OFwRead(
    _In_ OFW_IHANDLE Handle,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length);

BOOLEAN
OFwWrite(
    _In_ OFW_PHANDLE Handle,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length);

BOOLEAN
OFwInterpret(
    _In_ PCSTR Command,
    _Out_opt_ PULONG ReturnValue);
