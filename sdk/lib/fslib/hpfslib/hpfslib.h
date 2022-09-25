/*
 * PROJECT:     ReactOS HPFS filesystem library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Common header file
 * COPYRIGHT:   Copyright 2022 Dmitry Borisov (di.sean@protonmail.com)
 */

#ifndef _HPFSLIB_H_
#define _HPFSLIB_H_

#define NTOS_MODE_USER
#include <ndk/exfuncs.h>
#include <ndk/iofuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <fmifs/fmifs.h>
#include <ntddstor.h>

#include <pshpack1.h>
typedef struct _HPFS_BOOT_SECTOR
{
    UCHAR Jump[3];
    UCHAR OEMName[8];
    USHORT BytesPerSector;
    UCHAR SectorsPerCluster;
    USHORT ReservedSectors;
    UCHAR FATCount;
    USHORT RootEntries;
    USHORT Sectors;
    UCHAR Media;
    USHORT FATSectors;
    USHORT SectorsPerTrack;
    USHORT Heads;
    ULONG HiddenSectors;
    ULONG SectorsHuge;
    UCHAR Drive;
    UCHAR Res1;
    UCHAR ExtBootSignature;
    ULONG VolumeID;
    UCHAR VolumeLabel[11];
    UCHAR SysType[8];
    UCHAR Res2[446];
    ULONG Signature1;
} HPFS_BOOT_SECTOR, *PHPFS_BOOT_SECTOR;

typedef struct _HPFS_SUPER_BLOCK
{
    ULONG Signature1;
    ULONG Signature2;
    UCHAR Version;
    UCHAR FunctionalVersion;
    UCHAR Pad1[2];
    ULONG RootDir;
    ULONG Sectors;
    ULONG BadSectots;
    ULONG BitmapSectorList;
    ULONG Unused;
    ULONG BadSectorList;
    ULONG Unused2;
    ULONG LastCheckTime;
    ULONG LastOptimizeTime;
    ULONG DirBandSize;
    ULONG DirBandStart;
    ULONG DirBandEnd;
    ULONG DirBandBitmap;
    UCHAR Reserved[32];
    ULONG UserId;
    UCHAR Pad2[412];
} HPFS_SUPER_BLOCK, *PHPFS_SUPER_BLOCK;

typedef struct _HPFS_SPARE_BLOCK
{
    ULONG Signature1;
    ULONG Signature2;

    UCHAR Flags;
#define HPFS_SPARE_BLOCK_QUICK_FORMAT   0x20

    UCHAR Pad1[3];
    ULONG HotFixStart;
    ULONG HotFixesUsed;
    ULONG HotFixes;
    ULONG SpareDirBlocks;
    ULONG SpareDirBlocksFree;
    ULONG Codepage;
    ULONG Codepages;
    ULONG SuperBlockCrc;
    ULONG SpareBlockCrc;
    UCHAR Reserved[60];
    ULONG SpareDirBlock[100];
    UCHAR Pad2[4];
} HPFS_SPARE_BLOCK, *PHPFS_SPARE_BLOCK;

typedef struct _HPFS_CODEPAGE_INFO
{
    ULONG Signature;
    ULONG Count;
    ULONG Index;
    ULONG Next;
    struct
    {
        USHORT CountryCode;
        USHORT CodePage;
        ULONG Checksum;
        ULONG Lsn;
        USHORT Index;
        USHORT DbcsCount;
    } Info[31];
} HPFS_CODEPAGE_INFO, *PHPFS_CODEPAGE_INFO;

typedef struct _HPFS_CODEPAGE_DATA
{
    ULONG Signature;
    USHORT Count;
    USHORT Index;
    ULONG Checksum[3];
    USHORT Offset[3];
    struct
    {
        USHORT CountryCode;
        USHORT CodePage;
        USHORT DbcsRange;
        UCHAR MapTable[128];
        UCHAR DbcsRangeStart;
        UCHAR DbcsRangeEnd;
    } Page[3];
    UCHAR Pad[78];
} HPFS_CODEPAGE_DATA, *PHPFS_CODEPAGE_DATA;

typedef struct _HPFS_ALLEAF
{
    ULONG LogSector;
    ULONG Length;
    ULONG PhysSector;
} HPFS_ALLEAF, *PHPFS_ALLEAF;

typedef struct _HPFS_ALNODE
{
    ULONG LogSector;
    ULONG PhysSector;
} HPFS_ALNODE, *PHPFS_ALNODE;

typedef struct _HPFS_FNODE
{
    ULONG Signature;
    ULONG Unused[2];
    UCHAR NameLength;
    UCHAR Name[15];
    ULONG ParentDirectoryLsn;

    ULONG AclExternalLength;
    ULONG AclLsn;
    USHORT AclInternalLength;
    UCHAR AclFlags;

    UCHAR Unused2;

    ULONG EaExternalLength;
    ULONG EaLsn;
    USHORT EaInternalLength;
    UCHAR EaFlags;

    UCHAR DirFlags;

    UCHAR TreeFlags;
    UCHAR Pad[3];
    UCHAR Free;
    UCHAR Used;
    USHORT FreeOffset;
    union
    {
        HPFS_ALLEAF AlLeaf[8];
        HPFS_ALNODE AlNode[12];
    };
    ULONG FileSize;

    ULONG EaNeeded;
    UCHAR Unused3[16];
    USHORT EaOffset;
    UCHAR Reserved[10];
    UCHAR EaData[316];
} HPFS_FNODE, *PHPFS_FNODE;

typedef struct _HPFS_DIRENT
{
    USHORT Length;

    UCHAR Flags;
#define HPFS_DIRENT_SPECIAL     0x01
#define HPFS_DIRENT_DUMMY_END   0x08

    UCHAR Attributes;
#define HPFS_DIRENT_DIRECTORY   0x10

    ULONG FNodeLsn;
    ULONG LastModifiedTime;
    ULONG FileSize;
    ULONG LastAccessTime;
    ULONG CreationTime;
    ULONG EaLength;
    UCHAR Flex;
    UCHAR CodePage;
    UCHAR NameLength;
    _Field_size_(NameLength)
    CHAR FileName[ANYSIZE_ARRAY];
} HPFS_DIRENT, *PHPFS_DIRENT;

typedef struct _HPFS_DIRBLK
{
    ULONG Signature;
    ULONG FreeOffset;

    ULONG Flags;
#define HPFS_DIRENT_ROOT   0x01

    ULONG ParentLsn;
    ULONG SelfLsn;
    UCHAR Dirent[2028];
} HPFS_DIRBLK, *PHPFS_DIRBLK;
#include <poppack.h>

C_ASSERT(sizeof(HPFS_CODEPAGE_DATA) == 512);
C_ASSERT(sizeof(HPFS_CODEPAGE_INFO) == 512);
C_ASSERT(sizeof(HPFS_FNODE) == 512);
C_ASSERT(sizeof(HPFS_DIRBLK) == 2048);

#define HPFS_SUPER_BLOCK_SIGNATURE1    0xF995E849
#define HPFS_SUPER_BLOCK_SIGNATURE2    0xFA53E9C5
#define HPFS_SPARE_BLOCK_SIGNATURE1    0xF9911849
#define HPFS_SPARE_BLOCK_SIGNATURE2    0xFA5229C5
#define HPFS_CODEPAGE_DATA_SIGNATURE   0x894521F7
#define HPFS_CODEPAGE_INFO_SIGNATURE   0x494521F7
#define HPFS_FNODE_SIGNATURE           0xF7E40AAE
#define HPFS_DIRBLK_SIGNATURE          0x77E40AAE

#define HPFS_SECTOR_SIZE     512

/* 8 MB */
#define HPFS_BAND_SECTORS    (0x800000 / HPFS_SECTOR_SIZE)

#define HPFS_BITMAP_SIZE       (HPFS_BAND_SECTORS / 8)
#define HPFS_BITMAP_SECTORS    (HPFS_BITMAP_SIZE / HPFS_SECTOR_SIZE)

#define HPFS_BADBLOCK_LIST_SECTORS      4
#define HPFS_HOTFIX_LIST_SECTORS        4
#define HPFS_USER_ID_SECTORS            8
#define HPFS_DIRBLK_SECTORS             4
#define HPFS_FNODE_SECTORS              1
#define HPFS_SPARE_DIR_BLOCKS           20
#define HPFS_SPARE_DIR_BLOCKS_SECTORS   (HPFS_SPARE_DIR_BLOCKS * HPFS_DIRBLK_SECTORS)

#define HPFS_CONTROL_END     20

#define ROUND_UP(N, S) ((((N) + (S) - 1) / (S)) * (S))
#define ROUND_DOWN(N, S) ((N) & ~((S) - 1))

#define HPFS_FORMAT_BUFFER_SIZE       2048

#define HPFS_FORMAT_IO_BUFFER_SIZE    0x10000

C_ASSERT((HPFS_FORMAT_IO_BUFFER_SIZE % HPFS_SECTOR_SIZE) == 0);

typedef struct _HPFS_FORMAT_CONTEXT
{
    HANDLE FileHandle;
    PFMIFSCALLBACK Callback;
    ULONG SectorsWritten;
    ULONG SectorsNeeded;
    ULONG Percent;
    ULONG TotalSectorCount;
    ULONG TotalBitmapCount;
    ULONG DirBlkBitmap;
    ULONG DirBandBitmap;
    ULONG SpareDirBlocksBitmap;
    ULONG RootDirBitmap;
    ULONG BitmapListSectorCount;
    ULONG HotFixListSector;
    ULONG HotFixSectorCount;
    ULONG BadSectorListSector;
    ULONG RootDir;
    ULONG DirBlkStart;
    ULONG DirBandStart;
    ULONG DirBandSize;
    ULONG DirBandBitmapStart;
    ULONG SpareDirBlocksStart;
    ULONG CodePageSector;
    ULONG UserIdStart;
    ULONG CurrentSector;
    ULONG SuperBlockChecksum;
    UCHAR Buffer[HPFS_FORMAT_BUFFER_SIZE];
    PVOID IoBuffer;
} HPFS_FORMAT_CONTEXT, *PHPFS_FORMAT_CONTEXT;

#endif /* _HPFSLIB_H_ */
