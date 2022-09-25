/*
 * PROJECT:     ReactOS HPFS filesystem library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Filesystem Format support functions
 * COPYRIGHT:   Copyright 2022 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include "hpfslib.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

static
VOID
HpfsUpdateProgress(
    _In_ PHPFS_FORMAT_CONTEXT Context,
    _In_ ULONG Increment)
{
    ULONG NewPercent;

    Context->SectorsWritten += (ULONG64)Increment;

    NewPercent = (Context->SectorsWritten * 100ULL) / Context->SectorsNeeded;

    if (NewPercent > Context->Percent)
    {
        Context->Percent = NewPercent;
        if (Context->Callback)
        {
            Context->Callback(PROGRESS, 0, &Context->Percent);
        }
    }
}

static
ULONG
HpfsVolumeSerialNumber(VOID)
{
    LARGE_INTEGER SystemTime;
    TIME_FIELDS TimeFields;
    ULONG Serial;
    PUCHAR Buffer;

    NtQuerySystemTime(&SystemTime);
    RtlTimeToTimeFields(&SystemTime, &TimeFields);

    Buffer = (PUCHAR)&Serial;
    Buffer[0] = (UCHAR)(TimeFields.Year & 0xFF) + (UCHAR)(TimeFields.Hour & 0xFF);
    Buffer[1] = (UCHAR)(TimeFields.Year >> 8) + (UCHAR)(TimeFields.Minute & 0xFF);
    Buffer[2] = (UCHAR)(TimeFields.Month & 0xFF) + (UCHAR)(TimeFields.Second & 0xFF);
    Buffer[3] = (UCHAR)(TimeFields.Day & 0xFF) + (UCHAR)(TimeFields.Milliseconds & 0xFF);

    return Serial;
}

static
ULONG
HpfsChecksum(
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length)
{
    const UCHAR *Data = Buffer;
    ULONG i, Crc;

    Crc = 0;
    for (i = 0; i < Length; ++i)
    {
        Crc += *Data++;
        Crc = _rotl(Crc, 7);
    }

    return Crc;
}

static
inline
ULONG
HpfsNextSector(
    _In_ PHPFS_FORMAT_CONTEXT Context,
    _In_ ULONG Count)
{
    ULONG Result = Context->CurrentSector;

    Context->CurrentSector += Count;

    return Result;
}

static
BOOLEAN
HpfsFormatInitialize(
    _In_ PHPFS_FORMAT_CONTEXT Context,
    _In_ BOOLEAN QuickFormat,
    _In_ ULONG64 PartitionLength)
{
    ULONG DirBandSize, TotalBitmapCount, BitmapListSectorCount;

    Context->TotalSectorCount = ROUND_DOWN((PartitionLength / HPFS_SECTOR_SIZE),
                                           HPFS_BITMAP_SECTORS);

    TotalBitmapCount = ROUND_UP(Context->TotalSectorCount, HPFS_BAND_SECTORS) / HPFS_BAND_SECTORS;

    /* BitmapList is array of ULONGs used to index into the bitmap blocks */
    BitmapListSectorCount = ROUND_UP(TotalBitmapCount * sizeof(ULONG),
                                     HPFS_SECTOR_SIZE) / HPFS_SECTOR_SIZE;
    BitmapListSectorCount = ROUND_UP(BitmapListSectorCount, HPFS_BITMAP_SECTORS);

    Context->BitmapListSectorCount = BitmapListSectorCount;
    Context->TotalBitmapCount = TotalBitmapCount;
    Context->CurrentSector = HPFS_CONTROL_END + BitmapListSectorCount;
    Context->HotFixSectorCount = min(Context->TotalSectorCount / 400, 100);

    DirBandSize = 20 * max(PartitionLength / 0x100000, 10);
    DirBandSize = min(DirBandSize, HPFS_BAND_SECTORS - HPFS_BITMAP_SECTORS);

    Context->DirBandSize = DirBandSize;

    if (QuickFormat)
    {
        Context->SectorsNeeded = 3 + /* boot sector, super block, spare block */
                                 BitmapListSectorCount +
                                 HPFS_BADBLOCK_LIST_SECTORS +
                                 HPFS_HOTFIX_LIST_SECTORS +
                                 2 + /* code page */
                                 HPFS_BITMAP_SECTORS +
                                 HPFS_DIRBLK_SECTORS +
                                 HPFS_FNODE_SECTORS +
                                 TotalBitmapCount * HPFS_BITMAP_SECTORS;
    }
    else
    {
        /* Wipe out every data band */
        Context->SectorsNeeded = Context->TotalSectorCount;
    }

    return TRUE;
}

static
BOOLEAN
HpfsBootSectorInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context,
    _In_ BOOLEAN QuickFormat,
    _In_ PPARTITION_INFORMATION PartitionInfo,
    _In_ PDISK_GEOMETRY DiskGeometry,
    _In_ PCUNICODE_STRING Label)
{
    NTSTATUS Status;
    OEM_STRING VolumeLabel;
    LARGE_INTEGER FileOffset;
    IO_STATUS_BLOCK IoStatusBlock;
    PHPFS_BOOT_SECTOR BootSector;

    BootSector = (PHPFS_BOOT_SECTOR)Context->Buffer;

    RtlZeroMemory(BootSector, sizeof(HPFS_BOOT_SECTOR));

    BootSector->Jump[0] = 0xEB;
    BootSector->Jump[1] = 0x3C;
    BootSector->Jump[2] = 0x90;
    RtlCopyMemory(&BootSector->OEMName[0], "OS2 20.0 ", 8);
    BootSector->BytesPerSector = HPFS_SECTOR_SIZE;
    BootSector->SectorsPerCluster = 8;
    BootSector->ReservedSectors = 1;
    BootSector->FATCount = 0;
    BootSector->RootEntries = 512;
    BootSector->Media = 0xF8;
    BootSector->FATSectors = 0;
    BootSector->SectorsPerTrack = DiskGeometry->SectorsPerTrack;
    BootSector->Heads = DiskGeometry->TracksPerCylinder;
    BootSector->HiddenSectors = PartitionInfo->HiddenSectors;
    BootSector->SectorsHuge = Context->TotalSectorCount;
    BootSector->Drive = (DiskGeometry->MediaType == FixedMedia) ? 0x80 : 0x00;
    BootSector->ExtBootSignature = 0x28;
    BootSector->VolumeID = HpfsVolumeSerialNumber();
    BootSector->Signature1 = 0xAA550000;
    RtlCopyMemory(&BootSector->SysType[0], "HPFS    ", 8);
    if (!Label || !Label->Buffer)
    {
        RtlCopyMemory(&BootSector->VolumeLabel[0], "NO NAME    ", 11);
    }
    else
    {
        RtlUnicodeStringToOemString(&VolumeLabel, Label, TRUE);
        RtlFillMemory(&BootSector->VolumeLabel[0], 11, ' ');
        RtlCopyMemory(&BootSector->VolumeLabel[0],
                      VolumeLabel.Buffer,
                      max(VolumeLabel.Length, 11));
        RtlFreeOemString(&VolumeLabel);
    }

    FileOffset.QuadPart = 0;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         BootSector,
                         sizeof(HPFS_BOOT_SECTOR),
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, 1);

    return TRUE;
}

static
BOOLEAN
HpfsBitmapListInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context)
{
    ULONG BitmapNumber, StartSector;
    PULONG BitmapList;
    LARGE_INTEGER FileOffset;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    if (Context->BitmapListSectorCount * HPFS_SECTOR_SIZE > HPFS_FORMAT_BUFFER_SIZE)
    {
        BitmapList = RtlAllocateHeap(RtlGetProcessHeap(),
                                     0,
                                     Context->BitmapListSectorCount * HPFS_SECTOR_SIZE);
        if (!BitmapList)
            return FALSE;
    }
    else
    {
        BitmapList = (PULONG)Context->Buffer;
    }
    RtlZeroMemory(BitmapList, Context->BitmapListSectorCount * HPFS_SECTOR_SIZE);

    BitmapList[0] = HpfsNextSector(Context, Context->BitmapListSectorCount);

    StartSector = 0;
    for (BitmapNumber = 1; BitmapNumber < Context->TotalBitmapCount; ++BitmapNumber)
    {
        if (BitmapNumber % 2)
        {
            /* At the end of data band */
            StartSector += HPFS_BAND_SECTORS * 2 - HPFS_BITMAP_SECTORS;

            if (StartSector + (HPFS_BITMAP_SECTORS - 1) > Context->TotalSectorCount)
            {
                StartSector = Context->TotalSectorCount - HPFS_BITMAP_SECTORS;
            }
        }
        else
        {
            /* At the beginning */
            StartSector += HPFS_BITMAP_SECTORS;
        }

        DPRINT("Bitmap %u at %08lx-%08lx\n",
               BitmapNumber, StartSector, StartSector + (HPFS_BITMAP_SECTORS - 1));

        BitmapList[BitmapNumber] = StartSector;
    }

    /* The bitmap block starts just after the end of the control blocks */
    FileOffset.QuadPart = HPFS_CONTROL_END * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         BitmapList,
                         Context->BitmapListSectorCount * HPFS_SECTOR_SIZE,
                         &FileOffset,
                         NULL);

    if (BitmapList != (PULONG)Context->Buffer)
        RtlFreeHeap(RtlGetProcessHeap(), 0, BitmapList);

    HpfsUpdateProgress(Context, Context->BitmapListSectorCount);

    if (!NT_SUCCESS(Status))
        return FALSE;

    return TRUE;
}

static
BOOLEAN
HpfsBadSectorsListInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context)
{
    LARGE_INTEGER FileOffset;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    RtlZeroMemory(Context->Buffer, HPFS_BADBLOCK_LIST_SECTORS * HPFS_SECTOR_SIZE);

    Context->BadSectorListSector = HpfsNextSector(Context, HPFS_BADBLOCK_LIST_SECTORS);

    FileOffset.QuadPart = Context->BadSectorListSector * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         Context->Buffer,
                         HPFS_BADBLOCK_LIST_SECTORS * HPFS_SECTOR_SIZE,
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, HPFS_BADBLOCK_LIST_SECTORS);

    return TRUE;
}

static
BOOLEAN
HpfsHotfixListInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context,
    _In_ BOOLEAN QuickFormat)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    NTSTATUS Status;
    ULONG i, HotFixListSector, HotFixSectorCount;
    PULONG HotFixList;

    HotFixSectorCount = Context->HotFixSectorCount;
    Context->HotFixListSector =
    HotFixListSector = HpfsNextSector(Context, HPFS_HOTFIX_LIST_SECTORS + HotFixSectorCount);

    HotFixList = (PULONG)Context->Buffer;

    RtlZeroMemory(HotFixList, HPFS_HOTFIX_LIST_SECTORS * HPFS_SECTOR_SIZE);

    HotFixListSector += HPFS_HOTFIX_LIST_SECTORS;

    for (i = 0; i < HotFixSectorCount; ++i)
    {
        HotFixList[i + HotFixSectorCount] = HotFixListSector + i;
    }

    FileOffset.QuadPart = Context->HotFixListSector * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         HotFixList,
                         HPFS_HOTFIX_LIST_SECTORS * HPFS_SECTOR_SIZE,
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, HPFS_HOTFIX_LIST_SECTORS);

    return TRUE;
}

static
BOOLEAN
HpfsUserIdInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context,
    _In_ BOOLEAN QuickFormat)
{
    Context->UserIdStart = HpfsNextSector(Context, HPFS_USER_ID_SECTORS);

    return TRUE;
}

static
BOOLEAN
HpfsCodePageInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context)
{
    PHPFS_CODEPAGE_DATA CodePageData;
    PHPFS_CODEPAGE_INFO CodePageInfo;
    LCID SystemLocaleId;
    LARGE_INTEGER FileOffset;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;
    ULONG i;

    CodePageData = (PHPFS_CODEPAGE_DATA)Context->Buffer;

    RtlZeroMemory(CodePageData, sizeof(HPFS_CODEPAGE_DATA) + sizeof(HPFS_CODEPAGE_INFO));

    for (i = 0; i < sizeof(CodePageData->Page[0].MapTable); ++i)
    {
        CodePageData->Page[0].MapTable[i] = i | 0x80;
    }

    NtQueryDefaultLocale(FALSE, &SystemLocaleId);
    switch (PRIMARYLANGID(SystemLocaleId))
    {
        case LANG_CHINESE:
        {
            if (SUBLANGID(SystemLocaleId) == SUBLANG_CHINESE_SIMPLIFIED)
            {
                CodePageData->Page[0].CountryCode = 86;
                CodePageData->Page[0].CodePage = 936;
                CodePageData->Page[0].DbcsRangeStart = 161;
            }
            else
            {
                CodePageData->Page[0].CountryCode = 88;
                CodePageData->Page[0].CodePage = 950;
                CodePageData->Page[0].DbcsRangeStart = 129;
            }
            CodePageData->Page[0].DbcsRange = 1;
            CodePageData->Page[0].DbcsRangeEnd = 254;
            break;
        }

        case LANG_JAPANESE:
        {
            CodePageData->Page[0].CountryCode = 81;
            CodePageData->Page[0].CodePage = 932;
            CodePageData->Page[0].DbcsRange = 2;
            CodePageData->Page[0].DbcsRangeStart = 129;
            CodePageData->Page[0].DbcsRangeEnd = 159;
            break;
        }

        case LANG_KOREAN:
        {
            CodePageData->Page[0].CountryCode = 82;
            CodePageData->Page[0].CodePage = 949;
            CodePageData->Page[0].DbcsRange = 1;
            CodePageData->Page[0].DbcsRangeStart = 129;
            CodePageData->Page[0].DbcsRangeEnd = 254;
            break;
        }

        default:
        {
            static const CHAR HpfsDefaultSet[] =
            {
                0x80, 0x9A, 0x45, 0x41, 0x8E, 0x41, 0x8F, 0x80,
                0x45, 0x45, 0x45, 0x49, 0x49, 0x49, 0x8E, 0x8F,
                0x90, 0x92, 0x92, 0x4F, 0x99, 0x4F, 0x55, 0x55,
                0x59, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
                0x41, 0x49, 0x4F, 0x55
            };

            CodePageData->Page[0].CountryCode = 1;
            CodePageData->Page[0].CodePage = 850;
            CodePageData->Page[0].DbcsRange = 0;
            CodePageData->Page[0].DbcsRangeStart = 0;
            CodePageData->Page[0].DbcsRangeEnd = 0;

            RtlCopyMemory(&CodePageData->Page[0].MapTable[0],
                          HpfsDefaultSet,
                          sizeof(HpfsDefaultSet));
            break;
        }
    }

    CodePageData->Signature = HPFS_CODEPAGE_DATA_SIGNATURE;
    CodePageData->Count = 1;
    CodePageData->Offset[0] = FIELD_OFFSET(HPFS_CODEPAGE_DATA, Page[0]);
    CodePageData->Checksum[0] = HpfsChecksum(&CodePageData->Page[0],
                                             sizeof(CodePageData->Page[0]));

    Context->CodePageSector = HpfsNextSector(Context, 2);

    FileOffset.QuadPart = (Context->CodePageSector + 1) * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         CodePageData,
                         sizeof(HPFS_CODEPAGE_DATA),
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, 1);

    CodePageInfo = (PHPFS_CODEPAGE_INFO)(CodePageData + 1);
    CodePageInfo->Signature = HPFS_CODEPAGE_INFO_SIGNATURE;
    CodePageInfo->Count = 1;
    CodePageInfo->Info[0].CountryCode = CodePageData->Page[0].CountryCode;
    CodePageInfo->Info[0].CodePage = CodePageData->Page[0].CodePage;
    CodePageInfo->Info[0].Checksum = CodePageData->Checksum[0];
    CodePageInfo->Info[0].DbcsCount = CodePageData->Page[0].DbcsRange;
    CodePageInfo->Info[0].Lsn = Context->CodePageSector + 1;

    FileOffset.QuadPart = Context->CodePageSector * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         CodePageInfo,
                         sizeof(HPFS_CODEPAGE_INFO),
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, 1);

    return TRUE;
}

static
BOOLEAN
HpfsDirBandInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context,
    _In_ BOOLEAN QuickFormat)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    NTSTATUS Status;
    ULONG SeekCenter, SeekCenterBitmap;
    RTL_BITMAP Bitmap;

    SeekCenter = ROUND_DOWN(Context->TotalSectorCount, (HPFS_BAND_SECTORS * 2));
    SeekCenter /= 2;

    if (!SeekCenter)
    {
        SeekCenter = Context->TotalSectorCount / 2;

        Context->DirBandBitmapStart = SeekCenter;
        Context->DirBlkStart = SeekCenter - HPFS_DIRBLK_SECTORS;
        Context->RootDir = SeekCenter + HPFS_BITMAP_SECTORS;
        Context->DirBandStart = HpfsNextSector(Context, Context->DirBandSize);
        Context->SpareDirBlocksStart = HpfsNextSector(Context, HPFS_SPARE_DIR_BLOCKS_SECTORS);
    }
    else
    {
        Context->DirBandBitmapStart = SeekCenter - (2 * HPFS_BITMAP_SECTORS) - HPFS_DIRBLK_SECTORS;
        Context->DirBlkStart = SeekCenter - (2 * HPFS_BITMAP_SECTORS);

        Context->DirBlkBitmap = Context->DirBlkStart / HPFS_BAND_SECTORS;
        Context->DirBandBitmap = Context->DirBlkBitmap + 1;

        SeekCenterBitmap = SeekCenter / HPFS_BAND_SECTORS;

        if (SeekCenterBitmap % 2)
        {
            Context->DirBandStart = SeekCenter;
            Context->RootDir = SeekCenter - ROUND_UP(HPFS_FNODE_SECTORS, HPFS_BITMAP_SECTORS);
        }
        else
        {
            Context->DirBandStart = SeekCenter + HPFS_BITMAP_SECTORS;
        }

        if (Context->DirBandSize > (HPFS_BAND_SECTORS -
                                    HPFS_BITMAP_SECTORS - HPFS_SPARE_DIR_BLOCKS_SECTORS))
        {
            Context->SpareDirBlocksStart = ROUND_UP(Context->DirBandStart, HPFS_BAND_SECTORS);

            if ((SeekCenterBitmap % 2) == 0)
                Context->SpareDirBlocksStart += HPFS_BITMAP_SECTORS;
        }
        else
        {
            Context->SpareDirBlocksStart = Context->DirBandStart + Context->DirBandSize;
        }

        if ((SeekCenterBitmap % 2) == 0)
        {
            Context->RootDir = Context->SpareDirBlocksStart + HPFS_SPARE_DIR_BLOCKS_SECTORS;
        }

        Context->SpareDirBlocksBitmap = Context->SpareDirBlocksStart / HPFS_BAND_SECTORS;
        Context->RootDirBitmap = Context->RootDir / HPFS_BAND_SECTORS;
    }

    RtlInitializeBitMap(&Bitmap, (PULONG)Context->Buffer, HPFS_BAND_SECTORS);
    RtlClearAllBits(&Bitmap);

    RtlSetBits(&Bitmap, 0, Context->DirBandSize / HPFS_DIRBLK_SECTORS);

    FileOffset.QuadPart = (ULONG64)Context->DirBandBitmapStart * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         Context->Buffer,
                         HPFS_BITMAP_SECTORS * HPFS_SECTOR_SIZE,
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, HPFS_BITMAP_SECTORS);

    return TRUE;
}

static
BOOLEAN
HpfsFnodeInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context)
{
    PHPFS_FNODE Fnode;
    LARGE_INTEGER FileOffset;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    Fnode = (PHPFS_FNODE)Context->Buffer;

    RtlZeroMemory(Fnode, sizeof(HPFS_FNODE));

    Fnode->Signature = HPFS_FNODE_SIGNATURE;
    Fnode->ParentDirectoryLsn = Context->RootDir;
    Fnode->DirFlags = 1;
    Fnode->Free = RTL_NUMBER_OF_FIELD(HPFS_FNODE, AlLeaf) - 1;
    Fnode->Used = 1;
    Fnode->FreeOffset = FIELD_OFFSET(HPFS_FNODE, AlLeaf[1]) - FIELD_OFFSET(HPFS_FNODE, TreeFlags);
    Fnode->EaOffset = FIELD_OFFSET(HPFS_FNODE, EaData);
    Fnode->AlLeaf[0].PhysSector = Context->DirBlkStart;
    Fnode->AlLeaf[1].LogSector = 0xFFFFFFFF;

    FileOffset.QuadPart = (ULONG64)Fnode->ParentDirectoryLsn * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         Fnode,
                         sizeof(HPFS_FNODE),
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, HPFS_FNODE_SECTORS);

    return TRUE;
}

static
BOOLEAN
HpfsDirBlockInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context)
{
    LARGE_INTEGER FileOffset;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;
    ULONG Seconds;
    PHPFS_DIRBLK Dirblock;
    PHPFS_DIRENT Dirent;
    LARGE_INTEGER SystemTime;

    Dirblock = (PHPFS_DIRBLK)Context->Buffer;

    RtlZeroMemory(Dirblock, sizeof(HPFS_DIRBLK));

    Dirblock->Signature = HPFS_DIRBLK_SIGNATURE;
    Dirblock->Flags = HPFS_DIRENT_ROOT;
    Dirblock->ParentLsn = Context->RootDir;
    Dirblock->SelfLsn = Context->DirBlkStart;

    /* The special ".." entry */
    Dirent = (PHPFS_DIRENT)&Dirblock->Dirent[0];
    Dirent->Length = ROUND_UP(FIELD_OFFSET(HPFS_DIRENT, FileName[2]), sizeof(ULONG));
    Dirent->Flags = HPFS_DIRENT_SPECIAL;
    Dirent->Attributes = HPFS_DIRENT_DIRECTORY;
    Dirent->FNodeLsn = Context->RootDir;
    NtQuerySystemTime(&SystemTime);
    RtlTimeToSecondsSince1970(&SystemTime, &Seconds);
    Dirent->LastModifiedTime =
    Dirent->LastAccessTime =
    Dirent->CreationTime = Seconds;
    Dirent->FileSize = 5;
    Dirent->NameLength = 2;
    Dirent->FileName[0] = 1;
    Dirent->FileName[1] = 1;

    /* Dummy end record */
    Dirent = (PHPFS_DIRENT)((ULONG_PTR)Dirent + Dirent->Length);
    Dirent->Length = ROUND_UP(FIELD_OFFSET(HPFS_DIRENT, FileName[1]), sizeof(ULONG));
    Dirent->Flags = HPFS_DIRENT_DUMMY_END;
    Dirent->NameLength = 1;
    Dirent->FileName[0] = 0xFF;

    Dirblock->FreeOffset = ((ULONG_PTR)Dirent + (ULONG_PTR)Dirent->Length) - (ULONG_PTR)Dirblock;

    FileOffset.QuadPart = (ULONG64)Context->DirBlkStart * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         Dirblock,
                         sizeof(HPFS_DIRBLK),
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, HPFS_DIRBLK_SECTORS);

    return TRUE;
}

static
BOOLEAN
HpfsSuperBlockInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context)
{
    PHPFS_SUPER_BLOCK SuperBlock;
    LARGE_INTEGER FileOffset;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    SuperBlock = (PHPFS_SUPER_BLOCK)Context->Buffer;

    RtlZeroMemory(SuperBlock, sizeof(HPFS_SUPER_BLOCK));

    SuperBlock->Signature1 = HPFS_SUPER_BLOCK_SIGNATURE1;
    SuperBlock->Signature2 = HPFS_SUPER_BLOCK_SIGNATURE2;
    SuperBlock->Version = 2;
    SuperBlock->FunctionalVersion = (Context->TotalSectorCount >= 0x400000) ? 3 : 2;
    SuperBlock->RootDir = Context->RootDir;
    SuperBlock->Sectors = Context->TotalSectorCount;
    SuperBlock->BitmapSectorList = HPFS_CONTROL_END;
    SuperBlock->BadSectorList = Context->BadSectorListSector;
    SuperBlock->DirBandSize = Context->DirBandSize;
    SuperBlock->DirBandStart = Context->DirBandStart;
    SuperBlock->DirBandEnd = Context->DirBandStart + Context->DirBandSize - 1;
    SuperBlock->DirBandBitmap = Context->DirBandBitmapStart;
    SuperBlock->UserId = Context->UserIdStart;

    Context->SuperBlockChecksum = HpfsChecksum(SuperBlock, sizeof(HPFS_SUPER_BLOCK));

    FileOffset.QuadPart = 16 * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         SuperBlock,
                         sizeof(HPFS_SUPER_BLOCK),
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, 1);

    return TRUE;
}

static
BOOLEAN
HpfsSpareBlockInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context,
    _In_ BOOLEAN QuickFormat)
{
    LARGE_INTEGER FileOffset;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;
    PHPFS_SPARE_BLOCK SpareBlock;
    ULONG SpareDirBlocksStart, i;

    SpareBlock = (PHPFS_SPARE_BLOCK)Context->Buffer;

    RtlZeroMemory(SpareBlock, sizeof(HPFS_SPARE_BLOCK));

    SpareBlock->Signature1 = HPFS_SPARE_BLOCK_SIGNATURE1;
    SpareBlock->Signature2 = HPFS_SPARE_BLOCK_SIGNATURE2;
    SpareBlock->Flags = QuickFormat ? HPFS_SPARE_BLOCK_QUICK_FORMAT : 0;
    SpareBlock->HotFixStart = Context->HotFixListSector;
    SpareBlock->HotFixes = Context->HotFixSectorCount;
    SpareBlock->SpareDirBlocks = HPFS_SPARE_DIR_BLOCKS;
    SpareBlock->SpareDirBlocksFree = HPFS_SPARE_DIR_BLOCKS;
    SpareBlock->Codepage = Context->CodePageSector;
    SpareBlock->Codepages = 1;
    SpareBlock->SuperBlockCrc = Context->SuperBlockChecksum;

    SpareDirBlocksStart = Context->SpareDirBlocksStart;
    for (i = 0; i < HPFS_SPARE_DIR_BLOCKS; ++i)
    {
        SpareBlock->SpareDirBlock[i] = SpareDirBlocksStart;

        SpareDirBlocksStart += HPFS_DIRBLK_SECTORS;
    }

    SpareBlock->SpareBlockCrc = HpfsChecksum(SpareBlock, sizeof(HPFS_SPARE_BLOCK));

    FileOffset.QuadPart = 17 * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         SpareBlock,
                         sizeof(HPFS_SPARE_BLOCK),
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, 1);

    return TRUE;
}

static
BOOLEAN
HpfsBitmapInstall(
    _In_ PHPFS_FORMAT_CONTEXT Context,
    _In_ BOOLEAN QuickFormat)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER FileOffset;
    NTSTATUS Status;
    RTL_BITMAP Bitmap;
    ULONG BitmapNumber, StartSector;
    ULONG LastSectorIndex;

    RtlInitializeBitMap(&Bitmap, (PULONG)Context->Buffer, HPFS_BAND_SECTORS);
    RtlClearAllBits(&Bitmap);

    RtlSetBits(&Bitmap,
               Context->CurrentSector,
               min(HPFS_BAND_SECTORS - Context->CurrentSector, Context->TotalSectorCount));

    if (Context->DirBandStart < HPFS_BAND_SECTORS)
    {
        RtlClearBits(&Bitmap,
                     Context->DirBlkStart,
                     Context->RootDir - Context->DirBlkStart);
    }
    else if (Context->DirBandBitmapStart < HPFS_BAND_SECTORS)
    {
        RtlClearBits(&Bitmap,
                     Context->DirBandBitmapStart,
                     Context->RootDir - Context->DirBandBitmapStart);
    }

    FileOffset.QuadPart = (HPFS_CONTROL_END + Context->BitmapListSectorCount) * HPFS_SECTOR_SIZE;
    Status = NtWriteFile(Context->FileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         Context->Buffer,
                         HPFS_BITMAP_SECTORS * HPFS_SECTOR_SIZE,
                         &FileOffset,
                         NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;
    HpfsUpdateProgress(Context, HPFS_BITMAP_SECTORS);

    StartSector = 0;
    for (BitmapNumber = 1; BitmapNumber < Context->TotalBitmapCount; ++BitmapNumber)
    {
        RtlSetAllBits(&Bitmap);

        if (BitmapNumber % 2)
        {
            /* At the end of data band */
            StartSector += HPFS_BAND_SECTORS * 2 - HPFS_BITMAP_SECTORS;

            if (StartSector + (HPFS_BITMAP_SECTORS - 1) > Context->TotalSectorCount)
            {
                StartSector = Context->TotalSectorCount - HPFS_BITMAP_SECTORS;
            }

            RtlClearBits(&Bitmap, HPFS_BAND_SECTORS - HPFS_BITMAP_SECTORS, HPFS_BITMAP_SECTORS);
        }
        else
        {
            /* At the beginning */
            StartSector += HPFS_BITMAP_SECTORS;

            RtlClearBits(&Bitmap, 0, HPFS_BITMAP_SECTORS);
        }

        if (BitmapNumber == Context->DirBlkBitmap)
        {
            RtlClearBits(&Bitmap,
                         Context->DirBlkStart % HPFS_BAND_SECTORS,
                         HPFS_DIRBLK_SECTORS);
        }
        if (BitmapNumber == Context->DirBandBitmap)
        {
            RtlClearBits(&Bitmap,
                         Context->DirBandBitmap % HPFS_BAND_SECTORS,
                         Context->DirBandSize);
        }
        if (BitmapNumber == Context->SpareDirBlocksBitmap)
        {
            RtlClearBits(&Bitmap,
                         Context->SpareDirBlocksStart % HPFS_BAND_SECTORS,
                         HPFS_SPARE_DIR_BLOCKS_SECTORS);
        }
        if (BitmapNumber == Context->RootDirBitmap)
        {
            RtlClearBits(&Bitmap,
                         Context->RootDir % HPFS_BAND_SECTORS,
                         HPFS_FNODE_SECTORS);
        }

        /* Make sure free sectors won't exceed partition length */
        if (BitmapNumber == Context->TotalBitmapCount - 1)
        {
            if (BitmapNumber % 2)
            {
                LastSectorIndex = StartSector % HPFS_BAND_SECTORS;
            }
            else
            {
                LastSectorIndex = Context->TotalSectorCount % HPFS_BAND_SECTORS;
            }

            RtlClearBits(&Bitmap, LastSectorIndex, HPFS_BAND_SECTORS - LastSectorIndex);
        }

        FileOffset.QuadPart = (ULONG64)StartSector * HPFS_SECTOR_SIZE;
        Status = NtWriteFile(Context->FileHandle,
                             NULL,
                             NULL,
                             NULL,
                             &IoStatusBlock,
                             Context->Buffer,
                             HPFS_BITMAP_SECTORS * HPFS_SECTOR_SIZE,
                             &FileOffset,
                             NULL);
        if (!NT_SUCCESS(Status))
            return FALSE;
        HpfsUpdateProgress(Context, HPFS_BITMAP_SECTORS);
    }

    return TRUE;
}

static
VOID
HpfsRequestTrim(
    _In_ HANDLE FileHandle)
{
    DEVICE_MANAGE_DATA_SET_ATTRIBUTES DataSetAttributes;
    IO_STATUS_BLOCK IoStatusBlock;

    RtlZeroMemory(&DataSetAttributes, sizeof(DataSetAttributes));
    DataSetAttributes.Size = sizeof(DataSetAttributes);
    DataSetAttributes.Action = DeviceDsmAction_Trim;
    DataSetAttributes.Flags = DEVICE_DSM_FLAG_ENTIRE_DATA_SET_RANGE |
                              DEVICE_DSM_FLAG_TRIM_NOT_FS_ALLOCATED;

    NtDeviceIoControlFile(FileHandle,
                          NULL,
                          NULL,
                          NULL,
                          &IoStatusBlock,
                          IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES,
                          &DataSetAttributes,
                          sizeof(DataSetAttributes),
                          NULL,
                          0);
}

BOOLEAN
NTAPI
HpfsFormat(
    IN PUNICODE_STRING DriveRoot,
    IN PFMIFSCALLBACK Callback,
    IN BOOLEAN QuickFormat,
    IN BOOLEAN BackwardCompatible,
    IN MEDIA_TYPE MediaType,
    IN PUNICODE_STRING Label,
    IN ULONG ClusterSize)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    DISK_GEOMETRY DiskGeometry;
    GET_LENGTH_INFORMATION LengthInformation;
    PARTITION_INFORMATION PartitionInfo;
    PHPFS_FORMAT_CONTEXT Context = NULL;
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    DPRINT("HpfsFormat(DriveRoot '%wZ')\n", DriveRoot);

    // FIXME:
    UNREFERENCED_PARAMETER(BackwardCompatible);
    UNREFERENCED_PARAMETER(MediaType);

    UNREFERENCED_PARAMETER(ClusterSize);

    InitializeObjectAttributes(&ObjectAttributes, DriveRoot, 0, NULL, NULL);
    Status = NtOpenFile(&FileHandle,
                        FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtOpenFile() failed with status 0x%08lx\n", Status);
        return FALSE;
    }

    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_DISK_GET_LENGTH_INFO,
                                   NULL,
                                   0,
                                   &LengthInformation,
                                   sizeof(LengthInformation));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IOCTL_DISK_GET_LENGTH_INFO failed with status 0x%08lx\n", Status);
        NtClose(FileHandle);
        return FALSE;
    }

    if (LengthInformation.Length.QuadPart >= 0xFFFFFFFF)
    {
        DPRINT1("Too large volume 0x%I64x\n", LengthInformation.Length.QuadPart);
        NtClose(FileHandle);
        return FALSE;
    }
    // TODO: min

    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                   NULL,
                                   0,
                                   &DiskGeometry,
                                   sizeof(DiskGeometry));
    if (!NT_SUCCESS(Status))
    {
        DPRINT("IOCTL_DISK_GET_DRIVE_GEOMETRY failed with status 0x%08x\n", Status);
        NtClose(FileHandle);
        return FALSE;
    }

    if (DiskGeometry.BytesPerSector != HPFS_SECTOR_SIZE)
    {
        DPRINT1("Invalid sector size %u\n", DiskGeometry.BytesPerSector);
        NtClose(FileHandle);
        return FALSE;
    }

    Status = NtDeviceIoControlFile(FileHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_DISK_GET_PARTITION_INFO,
                                   NULL,
                                   0,
                                   &PartitionInfo,
                                   sizeof(PartitionInfo));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IOCTL_DISK_GET_PARTITION_INFO failed with status 0x%08x\n", Status);
        NtClose(FileHandle);
        return FALSE;
    }

    if (Callback)
    {
        ULONG Percent = 0;
        Callback(PROGRESS, 0, (PVOID)&Percent);
    }

    NtFsControlFile(FileHandle,
                    NULL,
                    NULL,
                    NULL,
                    &IoStatusBlock,
                    FSCTL_LOCK_VOLUME,
                    NULL,
                    0,
                    NULL,
                    0);

    HpfsRequestTrim(FileHandle);

    Context = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(HPFS_FORMAT_CONTEXT));
    if (!Context)
        goto Cleanup;

    Context->FileHandle = FileHandle;
    Context->Callback = Callback;

    if (!HpfsFormatInitialize(Context, QuickFormat, LengthInformation.Length.QuadPart))
        goto Cleanup;
    if (!HpfsBootSectorInstall(Context, QuickFormat, &PartitionInfo, &DiskGeometry, Label))
        goto Cleanup;
    if (!HpfsBitmapListInstall(Context))
        goto Cleanup;
    if (!HpfsBadSectorsListInstall(Context))
        goto Cleanup;
    if (!HpfsHotfixListInstall(Context, QuickFormat))
        goto Cleanup;
    if (!HpfsUserIdInstall(Context, QuickFormat))
        goto Cleanup;
    if (!HpfsCodePageInstall(Context))
        goto Cleanup;
    if (!HpfsDirBandInstall(Context, QuickFormat))
        goto Cleanup;
    if (!HpfsFnodeInstall(Context))
        goto Cleanup;
    if (!HpfsDirBlockInstall(Context))
        goto Cleanup;
    if (!HpfsSuperBlockInstall(Context))
        goto Cleanup;
    if (!HpfsSpareBlockInstall(Context, QuickFormat))
        goto Cleanup;
    if (!HpfsBitmapInstall(Context, QuickFormat))
        goto Cleanup;

    goto Success;

Cleanup:
    DPRINT1("Failed\n");
    __debugbreak();
Success:
    if (Context)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
    }

    NtFsControlFile(FileHandle,
                    NULL,
                    NULL,
                    NULL,
                    &IoStatusBlock,
                    FSCTL_DISMOUNT_VOLUME,
                    NULL,
                    0,
                    NULL,
                    0);

    NtFsControlFile(FileHandle,
                    NULL,
                    NULL,
                    NULL,
                    &IoStatusBlock,
                    FSCTL_UNLOCK_VOLUME,
                    NULL,
                    0,
                    NULL,
                    0);

    NtClose(FileHandle);

    return NT_SUCCESS(Status);
}
