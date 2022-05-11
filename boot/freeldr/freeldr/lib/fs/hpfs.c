/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     HPFS support
 * COPYRIGHT:   Copyright 2022 Dmitry Borisov <di.sean@protonmail.com>
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(FILESYSTEM);

/* GLOBALS ********************************************************************/

#define TAG_HPFS_VOLUME   'VfpH'
#define TAG_HPFS_FILE     'FfpH'

static
ARC_STATUS
HpfsClose(ULONG FileId);

static
ARC_STATUS
HpfsGetFileInformation(ULONG FileId, FILEINFORMATION* Information);

static
ARC_STATUS
HpfsOpen(CHAR* Path, OPENMODE OpenMode, ULONG* FileId);

static
ARC_STATUS
HpfsRead(ULONG FileId, VOID* Buffer, ULONG Size, ULONG* BytesRead);

static
ARC_STATUS
HpfsSeek(ULONG FileId, LARGE_INTEGER* Position, SEEKMODE SeekMode);

static const DEVVTBL HpfsFuncTable =
{
    HpfsClose,
    HpfsGetFileInformation,
    HpfsOpen,
    HpfsRead,
    HpfsSeek,
    L"pinball",
};

static PHPFS_VOLUME_INFO HpfsVolumes[MAX_FDS];

/* FUNCTIONS ******************************************************************/

static
BOOLEAN
HpfsDiskRead(
    _In_ ULONG DeviceId,
    _In_ ULONG Sector,
    _In_ ULONG Size,
    _Out_writes_bytes_all_(Size) PVOID Buffer)
{
    LARGE_INTEGER Position;
    ULONG Count;
    ARC_STATUS Status;

    Position.QuadPart = Sector * HPFS_SECTOR_SIZE;
    Status = ArcSeek(DeviceId, &Position, SeekAbsolute);
    if (Status != ESUCCESS)
    {
        return FALSE;
    }

    Status = ArcRead(DeviceId, Buffer, Size, &Count);
    if (Status != ESUCCESS || Count != Size)
    {
        return FALSE;
    }

    return TRUE;
}

static
PUCHAR
HpfsGetCodePage(
    _In_ PHPFS_VOLUME_INFO Volume,
    _In_ ULONG CodePage)
{
    return Volume->HackTable;
}

static
LONG
HpfsCompareFileName(
    _In_ PHPFS_VOLUME_INFO Volume,
    _In_ PCSTR FileName1,
    _In_ ULONG Length1,
    _In_ ULONG CodePage,
    _In_ PCSTR FileName2,
    _In_ ULONG Length2)
{
    PUCHAR MapTable;
    LONG Result = 0;
    ULONG Length = min(Length1, Length2);

    MapTable = HpfsGetCodePage(Volume, CodePage);

    while (!Result && Length--)
    {
        Result = MapTable[*FileName1++] - MapTable[*FileName2++];
    }

    if (!Result)
    {
        Result = Length1 - Length2;
    }

    return Result;
}

static
VOID
HpfsInvalidateDirentCache(
    _In_ PHPFS_VOLUME_INFO Volume)
{

}

static
PHPFS_DIRENT
HpfsSearchDirent(
    _In_ PHPFS_VOLUME_INFO Volume,
    _In_ PHPFS_DIRENT CurrentDirent,
    _In_ ULONG Flags,
    _In_ PCSTR FileName,
    _In_ ULONG NameLength)
{
    PHPFS_DIRENT Dirent;

    if (CurrentDirent)
    {
        if (!HpfsDiskRead(Volume->DeviceId,
                          CurrentDirent->FNodeSector,
                          sizeof(HPFS_FNODE),
                          &Volume->Buffer.FNode))
        {
            return NULL;
        }

        if (!HpfsDiskRead(Volume->DeviceId,
                          Volume->Buffer.FNode.ALeaf[0].PhysSector,
                          sizeof(HPFS_DNODE),
                          &Volume->Buffer.DNode))
        {
            return NULL;
        }
    }
    else
    {
        if (!HpfsDiskRead(Volume->DeviceId,
                          Volume->RootDir,
                          sizeof(HPFS_DNODE),
                          &Volume->Buffer.DNode))
        {
            return NULL;
        }
    }

    Dirent = (PHPFS_DIRENT)&Volume->Buffer.DNode.Dirent[0];

    while (TRUE)
    {
        LONG Result;

        /* Last dirent */
        if (Dirent->Flags & 0x08)
        {
            goto CheckNextLevel;
        }

        Result = HpfsCompareFileName(Volume,
                                     Dirent->FileName,
                                     Dirent->NameLength,
                                     Dirent->CodePage,
                                     FileName,
                                     NameLength);
        if (Result < 0)
        {
            Dirent = HPFS_NEXT_DIRENT(Dirent);
            continue;
        }
        else if (Result == 0)
        {
            return Dirent;
        }

CheckNextLevel:

        /* Go to the next level */
        if (Dirent->Flags & 0x04)
        {
            if (!HpfsDiskRead(Volume->DeviceId,
                              HPFS_NEXT_DNODE(Dirent),
                              sizeof(HPFS_DNODE),
                              &Volume->Buffer.DNode))
            {
                return NULL;
            }

            Dirent = (PHPFS_DIRENT)&Volume->Buffer.DNode.Dirent[0];
        }
        else
        {
            break;
        }
    }

    return NULL;
}

static
PCSTR
HpfsLastPathSeparator(
    _In_ PCSTR Path)
{
    PCSTR Last = NULL;

    while (*Path != ANSI_NULL)
    {
        if (*Path == '\\' || *Path == '/')
            Last = Path;

        ++Path;
    }

    return Last;
}

static
ULONG
HpfsPathComponentLength(
    _In_ PCSTR Path)
{
    ULONG Length = 0;

    while (*Path != ANSI_NULL)
    {
        if (*Path == '\\' || *Path == '/')
            break;

        ++Path;
        ++Length;
    }

    return Length;
}

static
BOOLEAN
HpfsLookupFileAt(
    _In_ PHPFS_VOLUME_INFO Volume,
    _In_ PCSTR Path,
    _Out_ PHPFS_FILE_HANDLE FileHandle)
{
    PCSTR Current, Last;
    ULONG Length;
    PHPFS_DIRENT Dirent = NULL;

    Last = HpfsLastPathSeparator(Path);

    Current = Path;

    do
    {
        Length = HpfsPathComponentLength(Current);

        Dirent = HpfsSearchDirent(Volume, Dirent, 0, Current, Length);
        if (!Dirent)
        {
            return FALSE;
        }

        Current += Length;

        if (Current == Last || Last == NULL)
            HpfsInvalidateDirentCache(Volume);

        if (*Current == ANSI_NULL)
            break;

        ++Current;
    }
    while (Length);

    FileHandle->Sector = Dirent->FNodeSector;

    if (!HpfsDiskRead(Volume->DeviceId,
                      Dirent->FNodeSector,
                      sizeof(HPFS_FNODE),
                      &Volume->Buffer.FNode))
    {
        return FALSE;
    }

    FileHandle->Size = Volume->Buffer.FNode.FileSize;

    return TRUE;
}

static
ULONG
HpfsReadFile(
    _In_ ULONG DeviceId,
    _In_ PHPFS_FNODE FNode,
    _In_ ULONG64 Offset,
    _In_ ULONG Size,
    _In_ PVOID Buffer)
{
    PUCHAR BufferPtr = Buffer;
    ULONG i, j, BytesRead = 0;

    if (FNode->TreeFlags & 0x80)
    {
        ASSERT(0);
    }

    for (i = 0; i < FNode->Used; ++i)
    {
        UCHAR FileBuffer[HPFS_SECTOR_SIZE];
        ULONG ReadSize;
        PHPFS_ALEAF ALeaf = &FNode->ALeaf[i];

        for (j = 0; j < ALeaf->Length; ++j)
        {
            if (Offset > HPFS_SECTOR_SIZE)
            {
                Offset -= HPFS_SECTOR_SIZE;
                continue;
            }

            if (!HpfsDiskRead(DeviceId,
                              ALeaf->PhysSector + j,
                              HPFS_SECTOR_SIZE,
                              FileBuffer))
            {
                return 0;
            }

            if (Offset)
            {
                ReadSize = min(HPFS_SECTOR_SIZE - Offset, Size);

                Offset = 0;
            }
            else
            {
                ReadSize = min(HPFS_SECTOR_SIZE, Size);
            }

            RtlCopyMemory(BufferPtr,
                          FileBuffer + Offset,
                          ReadSize);

            BufferPtr += ReadSize;
            Size -= ReadSize;
            BytesRead += ReadSize;

            if (!Size)
                break;
        }
    }

    return BytesRead;
}


/* I/O FUNCTIONS **************************************************************/

static
ARC_STATUS
HpfsClose(
    ULONG FileId)
{
    PHPFS_FILE_HANDLE FileHandle = FsGetDeviceSpecific(FileId);

    TRACE("Enter HpfsClose 0x%lx\n", FileId);

    FrLdrTempFree(FileHandle, TAG_HPFS_FILE);
    return ESUCCESS;
}

static
ARC_STATUS
HpfsGetFileInformation(
    ULONG FileId,
    FILEINFORMATION* Information)
{
    PHPFS_FILE_HANDLE FileHandle = FsGetDeviceSpecific(FileId);

    TRACE("Enter HpfsGetFileInformation 0x%lx\n", FileId);

    RtlZeroMemory(Information, sizeof(*Information));
    Information->EndingAddress.QuadPart = FileHandle->Size;
    Information->CurrentAddress.QuadPart = FileHandle->Offset;

    return ESUCCESS;
}

static
ARC_STATUS
HpfsOpen(
    CHAR* Path,
    OPENMODE OpenMode,
    ULONG* FileId)
{
    ULONG DeviceId;
    PHPFS_VOLUME_INFO Volume;
    PHPFS_FILE_HANDLE FileHandle;

    TRACE("Enter HpfsOpen %s 0x%lx\n", Path, FileId);

    if (OpenMode != OpenReadOnly)
        return EACCES;

    DeviceId = FsGetDeviceId(*FileId);
    Volume = HpfsVolumes[DeviceId];

    FileHandle = FrLdrTempAlloc(sizeof(HPFS_FILE_HANDLE), TAG_HPFS_FILE);
    if (!FileHandle)
        return ENOMEM;
    RtlZeroMemory(FileHandle, sizeof(HPFS_FILE_HANDLE));

    if (!HpfsLookupFileAt(Volume, Path, FileHandle))
    {
        FrLdrTempFree(FileHandle, TAG_HPFS_FILE);
        return ENOENT;
    }

    FsSetDeviceSpecific(*FileId, FileHandle);
    return ESUCCESS;
}

static
ARC_STATUS
HpfsRead(
    ULONG FileId,
    VOID* Buffer,
    ULONG Size,
    ULONG* BytesRead)
{
    ULONG DeviceId;
    HPFS_FNODE FNode;
    PHPFS_FILE_HANDLE FileHandle = FsGetDeviceSpecific(FileId);

    TRACE("Enter HpfsRead 0x%lx Size 0x%lx Offset 0x%I64x\n",
          FileId, Size, FileHandle->Offset);

    if (FileHandle->Offset >= FileHandle->Size)
    {
        return ESUCCESS;
    }

    DeviceId = FsGetDeviceId(FileId);

    if (!HpfsDiskRead(DeviceId, FileHandle->Sector, sizeof(FNode), &FNode))
        return ENOENT;

    *BytesRead = HpfsReadFile(DeviceId,
                              &FNode,
                              FileHandle->Offset,
                              Size,
                              Buffer);
    if (!*BytesRead)
        return ENOENT;

    FileHandle->Offset += *BytesRead;
    return ESUCCESS;
}

static
ARC_STATUS
HpfsSeek(
    ULONG FileId,
    LARGE_INTEGER* Position,
    SEEKMODE SeekMode)
{
    PHPFS_FILE_HANDLE FileHandle = FsGetDeviceSpecific(FileId);
    LARGE_INTEGER NewPosition = *Position;

    TRACE("Enter HpfsSeek 0x%lx 0x%lx\n", FileId, SeekMode);

    switch (SeekMode)
    {
        case SeekAbsolute:
            break;
        case SeekRelative:
            NewPosition.QuadPart += FileHandle->Offset;
            break;
        default:
            ASSERT(FALSE);
            return EINVAL;
    }

    if (NewPosition.QuadPart >= FileHandle->Size)
        return EINVAL;

    FileHandle->Offset = NewPosition.QuadPart;
    return ESUCCESS;
}

const
DEVVTBL*
HpfsMount(
    ULONG DeviceId)
{
    UCHAR Buffer[HPFS_SECTOR_SIZE];
    PHPFS_VOLUME_INFO Volume;
    PHPFS_SUPER_BLOCK SuperBlock = (PHPFS_SUPER_BLOCK)Buffer;
    PHPFS_SPARE_BLOCK SpareBlock = (PHPFS_SPARE_BLOCK)Buffer;
    PHPFS_FNODE FNode = (PHPFS_FNODE)Buffer;
    ULONG i;

    TRACE("Enter HpfsMount(%lu)\n", DeviceId);

    /* Read the SuperBlock */
    if (!HpfsDiskRead(DeviceId, 16, sizeof(Buffer), Buffer))
        return NULL;

    if (SuperBlock->Signature1 != 0xF995E849 ||
        SuperBlock->Signature2 != 0xFA53E9C5)
    {
        return NULL;
    }

    Volume = FrLdrTempAlloc(sizeof(HPFS_VOLUME_INFO), TAG_HPFS_VOLUME);
    if (!Volume)
        goto Failure;

    Volume->RootDir = SuperBlock->RootDir;
    Volume->DeviceId = DeviceId;

    /* Read the SpareBlock */
    if (!HpfsDiskRead(DeviceId, 17, sizeof(Buffer), Buffer))
        goto Failure;

    if (SpareBlock->Signature1 != 0xF9911849 ||
        SpareBlock->Signature2 != 0xFA5229C5)
    {
        goto Failure;
    }

    if (!HpfsDiskRead(DeviceId, Volume->RootDir, sizeof(HPFS_FNODE), FNode))
        goto Failure;

    Volume->RootDir = FNode->ALeaf[0].PhysSector;

    for (i = 0; i < 128; ++i)
    {
        Volume->HackTable[i] = i;
    }
    for (i = 'a'; i <= 'z'; ++i)
    {
        Volume->HackTable[i] = i & ~' ';
    }
    for (i = 128; i < 256; ++i)
    {
        Volume->HackTable[i] = i | 0x80;
    }

    HpfsVolumes[DeviceId] = Volume;

    return &HpfsFuncTable;

Failure:
    FrLdrTempFree(Volume, TAG_HPFS_VOLUME);
    return NULL;
}
