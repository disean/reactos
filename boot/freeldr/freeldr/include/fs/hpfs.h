/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     HPFS support definitions
 * COPYRIGHT:   Copyright 2022 Dmitry Borisov <di.sean@protonmail.com>
 */

#pragma once

#define HPFS_SECTOR_SIZE    0x200

typedef struct _HPFS_SUPER_BLOCK
{
    ULONG Signature1;
    ULONG Signature2;
    UCHAR Unused[4];
    ULONG RootDir;
    UCHAR Unused2[496];
} HPFS_SUPER_BLOCK, *PHPFS_SUPER_BLOCK;

typedef struct _HPFS_SPARE_BLOCK
{
    ULONG Signature1;
    ULONG Signature2;
    UCHAR Unused[504];
} HPFS_SPARE_BLOCK, *PHPFS_SPARE_BLOCK;

typedef struct _HPFS_ALEAF
{
    ULONG LogSector;
    ULONG Length;
    ULONG PhysSector;
} HPFS_ALEAF, *PHPFS_ALEAF;

typedef struct _HPFS_ANODE
{
    ULONG LogSector;
    ULONG PhysSector;
} HPFS_ANODE, *PHPFS_ANODE;

typedef struct _HPFS_FNODE
{
    ULONG Signature;
    ULONG Unused[2];
    UCHAR NameLength;
    UCHAR Name[15];
    UCHAR Unused2[27];
    UCHAR DirFlags;
    UCHAR TreeFlags;
    UCHAR Pad[3];
    UCHAR Unused3;
    UCHAR Used;
    UCHAR Unused4[2];
    union
    {
        HPFS_ALEAF ALeaf[8];
        HPFS_ANODE ANode[12];
    };
    ULONG FileSize;
} HPFS_FNODE, *PHPFS_FNODE;

typedef struct _HPFS_DNODE
{
    ULONG Signature;
    ULONG FreeOffset;
    ULONG Flags;
    ULONG Parent;
    ULONG Self;
    UCHAR Dirent[2028];
} HPFS_DNODE, *PHPFS_DNODE;

typedef struct _HPFS_DIRENT
{
    USHORT Length;
    UCHAR Flags;
    UCHAR Attributes;
    ULONG FNodeSector;
    UCHAR Unused[21];
    UCHAR CodePage;
    UCHAR NameLength;
    _Field_size_(NameLength)
    CHAR FileName[ANYSIZE_ARRAY];
} HPFS_DIRENT, *PHPFS_DIRENT;

typedef struct _HPFS_VOLUME_INFO
{
    ULONG DeviceId;
    ULONG RootDir;
    union
    {
        HPFS_DNODE DNode;
        HPFS_FNODE FNode;
        UCHAR Sector[HPFS_SECTOR_SIZE];
    } Buffer;
    HPFS_DNODE DNodeCache;
    UCHAR PathCache[261];
    UCHAR HackTable[256];
} HPFS_VOLUME_INFO, *PHPFS_VOLUME_INFO;

typedef struct _HPFS_FILE_HANDLE
{
    ULONG64 Offset;
    ULONG Sector;
    ULONG Size;
} HPFS_FILE_HANDLE, *PHPFS_FILE_HANDLE;

FORCEINLINE
PHPFS_DIRENT
HPFS_NEXT_DIRENT(
    _In_ PHPFS_DIRENT Dirent)
{
    return (PHPFS_DIRENT)(PVOID)((ULONG_PTR)Dirent + Dirent->Length);
}

FORCEINLINE
ULONG
HPFS_NEXT_DNODE(
    _In_ PHPFS_DIRENT Dirent)
{
    return *(PULONG)((ULONG_PTR)Dirent + Dirent->Length - sizeof(ULONG));
}

const DEVVTBL* HpfsMount(ULONG DeviceId);
