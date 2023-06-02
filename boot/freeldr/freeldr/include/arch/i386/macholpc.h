/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Machine-specific header file for OLPC XO-1
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov (di.sean@protonmail.com)
 */

#pragma once

/* pcmem.c */
extern BIOS_MEMORY_MAP PcBiosMemoryMap[];
extern ULONG PcBiosMapCount;

/* Platform-specific boot drive and partition numbers */
extern UCHAR FrldrBootDrive;
extern ULONG FrldrBootPartition;

/* hwdisk.c */
BOOLEAN PcInitializeBootDevices(VOID);

typedef struct _PSF2_HEADER
{
    ULONG Magic;
    ULONG Version;
    ULONG HeaderSize;
    ULONG Flags;
    ULONG Length;
    ULONG CharSize;
    ULONG Height;
    ULONG Width;
} PSF2_HEADER, *PPSF2_HEADER;

LONG DiskReportError(BOOLEAN bShowError);

VOID
OlpcHwIdle(VOID);

PCONFIGURATION_COMPONENT_DATA
OlpcHwDetect(VOID);

BOOLEAN
OlpcDiskReadLogicalSectors(
    IN UCHAR DriveNumber,
    IN ULONGLONG SectorNumber,
    IN ULONG SectorCount,
    OUT PVOID Buffer);

BOOLEAN
OlpcDiskGetDriveGeometry(
    UCHAR DriveNumber,
    PGEOMETRY Geometry);

ULONG
OlpcDiskGetCacheableBlockCount(
    UCHAR DriveNumber);

PFREELDR_MEMORY_DESCRIPTOR
OFwMemGetMemoryMap(
    ULONG* MemoryMapSize);

VOID
OFwConsPutChar(
    int Ch);

BOOLEAN
OFwConsKbHit(VOID);

int
OFwConsGetCh(VOID);

TIMEINFO*
XboxGetTime(VOID);
