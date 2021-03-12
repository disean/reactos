/*
 * PROJECT:         ReactOS ISA PnP Bus driver
 * PURPOSE:         Hardware support code
 * COPYRIGHT:       Copyright 2010 Cameron Gutman (cameron.gutman@reactos.org)
 *                  Copyright 2020 Hervé Poussineau (hpoussin@reactos.org)
 *                  Copyright 2021 Dmitry Borisov (di.sean@protonmail.com)
 */

#include <isapnp.h>

#define NDEBUG
#include <debug.h>

typedef enum
{
    dfNotStarted,
    dfStarted,
    dfDone
} DEPEDENT_FUNCTIONS_STATE;

static
inline
VOID
WriteAddress(
    _In_ USHORT Address)
{
    WRITE_PORT_UCHAR((PUCHAR)ISAPNP_ADDRESS, Address);
}

static
inline
VOID
WriteData(
    _In_ USHORT Data)
{
    WRITE_PORT_UCHAR((PUCHAR)ISAPNP_WRITE_DATA, Data);
}

static
inline
UCHAR
ReadData(
    _In_ PUCHAR ReadDataPort)
{
    return READ_PORT_UCHAR(ReadDataPort);
}

static
inline
VOID
WriteByte(
    _In_ USHORT Address,
    _In_ USHORT Value)
{
    WriteAddress(Address);
    WriteData(Value);
}

static
inline
UCHAR
ReadByte(
    _In_ PUCHAR ReadDataPort,
    _In_ USHORT Address)
{
    WriteAddress(Address);
    return ReadData(ReadDataPort);
}

static
inline
USHORT
ReadWord(
    _In_ PUCHAR ReadDataPort,
    _In_ USHORT Address)
{
    return ((ReadByte(ReadDataPort, Address) << 8) |
            (ReadByte(ReadDataPort, Address + 1)));
}

static
inline
USHORT
ReadDoubleWord(
    _In_ PUCHAR ReadDataPort,
    _In_ USHORT Address)
{
    return ((ReadWord(ReadDataPort, Address) << 8) |
            (ReadWord(ReadDataPort, Address + 2)));
}

static
inline
VOID
SetReadDataPort(
    _In_ PUCHAR ReadDataPort)
{
    WriteByte(ISAPNP_READPORT, ((ULONG_PTR)ReadDataPort >> 2));
}

static
inline
VOID
EnterIsolationState(VOID)
{
    WriteAddress(ISAPNP_SERIALISOLATION);
}

static
inline
VOID
WaitForKey(VOID)
{
    WriteByte(ISAPNP_CONFIGCONTROL, ISAPNP_CONFIG_WAIT_FOR_KEY);
}

static
inline
VOID
ResetCsn(VOID)
{
    WriteByte(ISAPNP_CONFIGCONTROL, ISAPNP_CONFIG_RESET_CSN);
}

static
inline
VOID
Wake(
    _In_ USHORT Csn)
{
    WriteByte(ISAPNP_WAKE, Csn);
}

static
inline
USHORT
ReadResourceData(
    _In_ PUCHAR ReadDataPort)
{
    return ReadByte(ReadDataPort, ISAPNP_RESOURCEDATA);
}

static
inline
USHORT
ReadStatus(
    _In_ PUCHAR ReadDataPort)
{
    return ReadByte(ReadDataPort, ISAPNP_STATUS);
}

static
inline
VOID
WriteCsn(
    _In_ USHORT Csn)
{
    WriteByte(ISAPNP_CARDSELECTNUMBER, Csn);
}

static
inline
VOID
WriteLogicalDeviceNumber(
    _In_ USHORT LogDev)
{
    WriteByte(ISAPNP_LOGICALDEVICENUMBER, LogDev);
}

static
inline
VOID
ActivateDevice(
    _In_ PUCHAR ReadDataPort,
    _In_ USHORT LogDev)
{
    WriteLogicalDeviceNumber(LogDev);

    WriteByte(ISAPNP_IORANGECHECK,
              ReadByte(ReadDataPort, ISAPNP_IORANGECHECK) & ~2);

    WriteByte(ISAPNP_ACTIVATE, 1);
}

static
inline
VOID
DeactivateDevice(
    _In_ USHORT LogDev)
{
    WriteLogicalDeviceNumber(LogDev);
    WriteByte(ISAPNP_ACTIVATE, 0);
}

static
inline
USHORT
ReadIoBase(
    _In_ PUCHAR ReadDataPort,
    _In_ USHORT Index)
{
    return ReadWord(ReadDataPort, ISAPNP_IOBASE(Index));
}

static
inline
USHORT
ReadIrqNo(
    _In_ PUCHAR ReadDataPort,
    _In_ USHORT Index)
{
    return ReadByte(ReadDataPort, ISAPNP_IRQNO(Index));
}

static
inline
USHORT
ReadIrqType(
    _In_ PUCHAR ReadDataPort,
    _In_ USHORT Index)
{
    return ReadByte(ReadDataPort, ISAPNP_IRQTYPE(Index));
}

static
inline
USHORT
ReadDmaChannel(
    _In_ PUCHAR ReadDataPort,
    _In_ USHORT Index)
{
    return ReadByte(ReadDataPort, ISAPNP_DMACHANNEL(Index));
}

static
inline
USHORT
ReadMemoryBase(
    _In_ PUCHAR ReadDataPort,
    _In_range_(>=, 1) USHORT Index)
{
    return ReadWord(ReadDataPort, ISAPNP_MEMBASE(Index));
}

static
inline
USHORT
ReadMemoryLimit(
    _In_ PUCHAR ReadDataPort,
    _In_range_(>=, 1) USHORT Index)
{
    return ReadWord(ReadDataPort, ISAPNP_MEMLIMIT(Index));
}

static
inline
USHORT
ReadMemoryBase32(
    _In_ PUCHAR ReadDataPort,
    _In_range_(>=, 1) USHORT Index)
{
    return ReadDoubleWord(ReadDataPort, ISAPNP_MEMBASE32(Index));
}

static
inline
USHORT
ReadMemoryLimit32(
    _In_ PUCHAR ReadDataPort,
    _In_range_(>=, 1) USHORT Index)
{
    return ReadDoubleWord(ReadDataPort, ISAPNP_MEMLIMIT32(Index));
}

static
inline
VOID
HwDelay(VOID)
{
    KeStallExecutionProcessor(1000);
}

static
inline
UCHAR
NextLFSR(
    _In_ UCHAR Lfsr,
    _In_ UCHAR InputBit)
{
    UCHAR NextLfsr = Lfsr >> 1;

    NextLfsr |= (((Lfsr ^ NextLfsr) ^ InputBit)) << 7;

    return NextLfsr;
}

static
VOID
SendKey(VOID)
{
    UCHAR i, Lfsr;

    HwDelay();
    WriteAddress(0x00);
    WriteAddress(0x00);

    Lfsr = ISAPNP_LFSR_SEED;
    for (i = 0; i < 32; i++)
    {
        WriteAddress(Lfsr);
        Lfsr = NextLFSR(Lfsr, 0);
    }
}

static
USHORT
PeekByte(
    _In_ PUCHAR ReadDataPort)
{
    USHORT i;

    for (i = 0; i < 20; i++)
    {
        if (ReadStatus(ReadDataPort) & 0x01)
            return ReadResourceData(ReadDataPort);

        HwDelay();
    }

    return 0xFF;
}

static
VOID
Peek(
    _In_ PUCHAR ReadDataPort,
    _Out_writes_bytes_opt_(Length) PVOID Buffer,
    _In_ USHORT Length)
{
    USHORT i, Byte;

    for (i = 0; i < Length; i++)
    {
        Byte = PeekByte(ReadDataPort);
        if (Buffer)
            *((PUCHAR)Buffer + i) = Byte;
    }
}

static
CODE_SEG("PAGE")
VOID
PeekCached(
    _In_reads_bytes_(Length) PUCHAR ResourceData,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ USHORT Length)
{
    PUCHAR Dest = Buffer;

    PAGED_CODE();

    while (Length--)
    {
        *Dest++ = *ResourceData++;
    }
}

static
USHORT
IsaPnpChecksum(
    _In_ PISAPNP_IDENTIFIER Identifier)
{
    UCHAR i, j, Lfsr, Byte;

    Lfsr = ISAPNP_LFSR_SEED;
    for (i = 0; i < 8; i++)
    {
        Byte = *(((PUCHAR)Identifier) + i);
        for (j = 0; j < 8; j++)
        {
            Lfsr = NextLFSR(Lfsr, Byte);
            Byte >>= 1;
        }
    }

    return Lfsr;
}

static
CODE_SEG("PAGE")
VOID
IsaPnpExtractAscii(
    _Out_writes_bytes_(3) PUCHAR Buffer,
    _In_ USHORT CompressedData)
{
    PAGED_CODE();

    Buffer[0] = ((CompressedData >> 2) & 0x1F) + 'A' - 1;
    Buffer[1] = (((CompressedData & 0x3) << 3) | ((CompressedData >> 13) & 0x7)) + 'A' - 1;
    Buffer[2] = ((CompressedData >> 8) & 0x1F) + 'A' - 1;
}

#define CACHE_DATA(_Byte) \
    do { \
        if (MaxLength-- == 0) \
            return STATUS_BUFFER_OVERFLOW; \
        *Buffer++ = _Byte; \
    } while (0)

static
CODE_SEG("PAGE")
NTSTATUS
ReadTags(
    _In_ PUCHAR ReadDataPort,
    _Out_ PUCHAR Buffer,
    _In_ ULONG MaxLength,
    _Out_ PUSHORT MaxLogDev)
{
    PAGED_CODE();

    *MaxLogDev = 0;

    while (TRUE)
    {
        USHORT Tag, TagLen;

        Tag = PeekByte(ReadDataPort);
        if (Tag == 0)
        {
            DPRINT("Invalid tag\n");
            return STATUS_INVALID_PARAMETER_1;
        }
        CACHE_DATA(Tag);

        if (ISAPNP_IS_SMALL_TAG(Tag))
        {
            TagLen = ISAPNP_SMALL_TAG_LEN(Tag);
            Tag = ISAPNP_SMALL_TAG_NAME(Tag);
        }
        else
        {
            UCHAR Temp[2];

            Peek(ReadDataPort, &Temp, sizeof(Temp));
            CACHE_DATA(Temp[0]);
            CACHE_DATA(Temp[1]);

            TagLen = Temp[0] + (Temp[1] << 8);
            Tag = ISAPNP_LARGE_TAG_NAME(Tag);
        }

        if (Tag == 0xFF && TagLen == 0xFFFF)
        {
            DPRINT("Invalid tag\n");
            return STATUS_INVALID_PARAMETER_2;
        }

        if (TagLen > MaxLength)
            return STATUS_BUFFER_OVERFLOW;

        Peek(ReadDataPort, Buffer, TagLen);
        MaxLength -= TagLen;
        Buffer += TagLen;

        if (Tag == ISAPNP_TAG_LOGDEVID)
            (*MaxLogDev)++;

#if 1
        if (Tag == ISAPNP_TAG_END)
        {
            --Buffer;
            --Buffer;

            /* Start DF */
            *Buffer++ = (6 << 3) | 1;
            *Buffer++ = 0x03;

            /* MEM: Min 0:1000, Max 0:27FF, Align 0 Len 800 */
            *Buffer++ = 0x85; *Buffer++ = 0x11; *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* min */
            *Buffer++ = 0x00;
            *Buffer++ = 0x10;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* max */
            *Buffer++ = 0x00;
            *Buffer++ = 0x20;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* align */
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* len */
            *Buffer++ = 0x00;
            *Buffer++ = 0x08;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;

            /* Start DF */
            *Buffer++ = (6 << 3) | 0;

            /* MEM: Min 0:1000, Max 0:1000800, Align 0 Len 801 */
            *Buffer++ = 0x85; *Buffer++ = 0x11; *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* min */
            *Buffer++ = 0x00;
            *Buffer++ = 0x10;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* max */
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            *Buffer++ = 0x01;
            /* align */
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* len */
            *Buffer++ = 0x01;
            *Buffer++ = 0x08;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;

            /* Start DF */
            *Buffer++ = (6 << 3) | 1;
            *Buffer++ = 0x02;

            /* MEM: Min 0:1000, Max 0:2801, Align 0 Len 802 */
            *Buffer++ = 0x85; *Buffer++ = 0x11; *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* min */
            *Buffer++ = 0x00;
            *Buffer++ = 0x10;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* max */
            *Buffer++ = 0x00;
            *Buffer++ = 0x20;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* align */
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;
            /* len */
            *Buffer++ = 0x02;
            *Buffer++ = 0x08;
            *Buffer++ = 0x00;
            *Buffer++ = 0x00;

            /* End DF */
            *Buffer++ = (7 << 3) | 0;

            /* End */
            *Buffer++ = (15 << 3) | 1;
            *Buffer++ = 0;
        }
#endif

        if (Tag == ISAPNP_TAG_END)
            break;
    }

    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
NTSTATUS
ParseTags(
    _In_ PUCHAR ResourceData,
    _In_ USHORT LogDevToRead,
    _Inout_ PISAPNP_LOGICAL_DEVICE LogDevice)
{
    USHORT LogDev;
    DEPEDENT_FUNCTIONS_STATE DfState = dfNotStarted;
    ULONG NumberOfIo = 0,
          NumberOfIrq = 0,
          NumberOfDma = 0,
          NumberOfMemRange = 0,
          NumberOfMemRange32 = 0,
          NumberOfDepedentSet = -1;

    PAGED_CODE();

    DPRINT("%s for CSN %lu, LDN %lu\n", __FUNCTION__, LogDevice->CSN, LogDevice->LDN);

    LogDev = LogDevToRead + 1;

    while (TRUE)
    {
        USHORT Tag, TagLen;

        Tag = *ResourceData++;

        if (ISAPNP_IS_SMALL_TAG(Tag))
        {
            TagLen = ISAPNP_SMALL_TAG_LEN(Tag);
            Tag = ISAPNP_SMALL_TAG_NAME(Tag);
        }
        else
        {
            TagLen = *ResourceData++;
            TagLen += *ResourceData++ << 8;

            Tag = ISAPNP_LARGE_TAG_NAME(Tag);
        }

        switch (Tag)
        {
            case ISAPNP_TAG_LOGDEVID:
            {
                --LogDev;

                if (LogDev != 0 ||
                    (TagLen > sizeof(ISAPNP_LOGDEVID) ||
                     TagLen < (sizeof(ISAPNP_LOGDEVID) - 1)))
                {
                    goto SkipTag;
                }

                PeekCached(ResourceData, &LogDevice->LogDevId, TagLen);
                ResourceData += TagLen;

                DPRINT("Found tag 0x%X (len %d)\n"
                       "  VendorId 0x%04X\n"
                       "  ProdId   0x%04X\n"
                       "  Flags    0x%X\n",
                       Tag, TagLen,
                       LogDevice->LogDevId.VendorId,
                       LogDevice->LogDevId.ProdId,
                       LogDevice->LogDevId.Flags);

                break;
            }

            case ISAPNP_TAG_COMPATDEVID:
            {
                ISAPNP_COMPATID Temp;
                PISAPNP_COMPATIBLE_ID_ENTRY CompatibleId;

                if (LogDev != 0 || TagLen != sizeof(ISAPNP_COMPATID))
                    goto SkipTag;

                CompatibleId = ExAllocatePoolWithTag(PagedPool,
                                                     sizeof(ISAPNP_COMPATIBLE_ID_ENTRY),
                                                     TAG_ISAPNP);
                if (!CompatibleId)
                    return STATUS_INSUFFICIENT_RESOURCES;

                PeekCached(ResourceData, &Temp, TagLen);
                ResourceData += TagLen;

                DPRINT("Found tag 0x%X (len %d)\n"
                       "  VendorId 0x%04X\n"
                       "  ProdId   0x%04X\n",
                       Tag, TagLen,
                       Temp.VendorId,
                       Temp.ProdId);

                IsaPnpExtractAscii(CompatibleId->VendorId, Temp.VendorId);
                CompatibleId->ProdId = RtlUshortByteSwap(Temp.ProdId);

                InsertTailList(&LogDevice->CompatibleIdList, &CompatibleId->IdLink);

                break;
            }

            case ISAPNP_TAG_IRQ:
            {
                PISAPNP_IRQ_DESCRIPTION Description;

                if (LogDev != 0 ||
                    (TagLen > sizeof(ISAPNP_IRQ_DESCRIPTION) ||
                     TagLen < (sizeof(ISAPNP_IRQ_DESCRIPTION) - 1)) ||
                    NumberOfIrq >= RTL_NUMBER_OF(LogDevice->Irq))
                {
                    goto SkipTag;
                }

                if (DfState == dfStarted)
                {
                    if (NumberOfDepedentSet >= ISAPNP_MAX_ALTERNATIVES)
                        goto SkipTag;

                    Description = &LogDevice->Alternatives->Irq[NumberOfDepedentSet];
                }
                else
                {
                    Description = &LogDevice->Irq[NumberOfIrq].Description;

                    ++NumberOfIrq;
                }

                PeekCached(ResourceData, Description, TagLen);
                ResourceData += TagLen;

                if (TagLen == (sizeof(ISAPNP_IRQ_DESCRIPTION) - 1))
                    Description->Information |= 0x01;

                DPRINT("Found tag 0x%X (len %d)\n"
                       "  Mask        0x%X\n"
                       "  Information 0x%X\n",
                       Tag, TagLen,
                       Description->Mask,
                       Description->Information);

                break;
            }

            case ISAPNP_TAG_DMA:
            {
                PISAPNP_DMA_DESCRIPTION Description;

                if (LogDev != 0 || TagLen != sizeof(ISAPNP_DMA_DESCRIPTION) ||
                    NumberOfDma >= RTL_NUMBER_OF(LogDevice->Dma))
                {
                    goto SkipTag;
                }

                if (DfState == dfStarted)
                {
                    if (NumberOfDepedentSet >= ISAPNP_MAX_ALTERNATIVES)
                        goto SkipTag;

                    Description = &LogDevice->Alternatives->Dma[NumberOfDepedentSet];
                }
                else
                {
                    Description = &LogDevice->Dma[NumberOfDma].Description;

                    ++NumberOfDma;
                }

                PeekCached(ResourceData, Description, TagLen);
                ResourceData += TagLen;

                DPRINT("Found tag 0x%X (len %d)\n"
                       "  Mask        0x%X\n"
                       "  Information 0x%X\n",
                       Tag, TagLen,
                       Description->Mask,
                       Description->Information);

                break;
            }

            case ISAPNP_TAG_STARTDEP:
            {
                if (LogDev != 0 || TagLen > 1)
                    goto SkipTag;

                if (DfState == dfNotStarted)
                {
                    LogDevice->Alternatives = ExAllocatePoolZero(PagedPool,
                                                                 sizeof(ISAPNP_ALTERNATIVES),
                                                                 TAG_ISAPNP);
                    if (!LogDevice->Alternatives)
                        return STATUS_INSUFFICIENT_RESOURCES;

                    DfState = dfStarted;
                }
                else if (DfState != dfStarted)
                {
                    goto SkipTag;
                }

                ++NumberOfDepedentSet;
                ++LogDevice->Alternatives->Count;

                if (TagLen != 1)
                {
                    LogDevice->Alternatives->Priority[NumberOfDepedentSet] = 1;
                }
                else
                {
                    PeekCached(ResourceData,
                               &LogDevice->Alternatives->Priority[NumberOfDepedentSet],
                               TagLen);
                    ResourceData += TagLen;
                }

                DPRINT("*** Start depedent set %d, priority %d ***\n",
                       NumberOfDepedentSet,
                       LogDevice->Alternatives->Priority[NumberOfDepedentSet]);

                break;
            }

            case ISAPNP_TAG_ENDDEP:
            {
                if (LogDev != 0)
                    goto SkipTag;

                DfState = dfDone;

                ResourceData += TagLen;

                if (LogDevice->Alternatives->Io[0].Length)
                    ++NumberOfIo;
                if (LogDevice->Alternatives->Irq[0].Mask)
                    ++NumberOfIrq;
                if (LogDevice->Alternatives->Dma[0].Mask)
                    ++NumberOfDma;
                if (LogDevice->Alternatives->MemRange[0].Length)
                    ++NumberOfMemRange;
                if (LogDevice->Alternatives->MemRange32[0].Length)
                    ++NumberOfMemRange32;

                DPRINT("*** End of depedent set ***\n");

                break;
            }

            case ISAPNP_TAG_IOPORT:
            {
                PISAPNP_IO_DESCRIPTION Description;

                if (LogDev != 0 || TagLen != sizeof(ISAPNP_IO_DESCRIPTION) ||
                    NumberOfIo >= RTL_NUMBER_OF(LogDevice->Io))
                {
                    goto SkipTag;
                }

                if (DfState == dfStarted)
                {
                    if (NumberOfDepedentSet >= ISAPNP_MAX_ALTERNATIVES)
                        goto SkipTag;

                    Description = &LogDevice->Alternatives->Io[NumberOfDepedentSet];
                }
                else
                {
                    Description = &LogDevice->Io[NumberOfIo].Description;

                    ++NumberOfIo;
                }

                PeekCached(ResourceData, Description, TagLen);
                ResourceData += TagLen;

                DPRINT("Found tag 0x%X (len %d)\n"
                       "  Information 0x%X\n"
                       "  Minimum     0x%X\n"
                       "  Maximum     0x%X\n"
                       "  Alignment   0x%X\n"
                       "  Length      0x%X\n",
                       Tag, TagLen,
                       Description->Information,
                       Description->Minimum,
                       Description->Maximum,
                       Description->Alignment,
                       Description->Length);

                break;
            }

            case ISAPNP_TAG_FIXEDIO:
            {
                ISAPNP_FIXED_IO_DESCRIPTION Temp;
                PISAPNP_IO_DESCRIPTION Description;

                if (LogDev != 0 || TagLen != sizeof(ISAPNP_FIXED_IO_DESCRIPTION) ||
                    NumberOfIo >= RTL_NUMBER_OF(LogDevice->Io))
                {
                    goto SkipTag;
                }

                if (DfState == dfStarted)
                {
                    if (NumberOfDepedentSet >= ISAPNP_MAX_ALTERNATIVES)
                        goto SkipTag;

                    Description = &LogDevice->Alternatives->Io[NumberOfDepedentSet];
                }
                else
                {
                    Description = &LogDevice->Io[NumberOfIo].Description;

                    ++NumberOfIo;
                }

                PeekCached(ResourceData, &Temp, TagLen);
                ResourceData += TagLen;

                Description->Information = 0;
                Description->Minimum =
                Description->Maximum = Temp.IoBase;
                Description->Alignment = 1;
                Description->Length = Temp.Length;

                DPRINT("Found tag 0x%X (len %d)\n"
                       "  IoBase 0x%X\n"
                       "  Length 0x%X\n",
                       Tag, TagLen,
                       Temp.IoBase,
                       Temp.Length);

                break;
            }

            case ISAPNP_TAG_END:
            {
                if (LogDev == 0)
                    return STATUS_SUCCESS;
                else
                    return STATUS_REPARSE;
            }

            case ISAPNP_TAG_MEMRANGE:
            {
                PISAPNP_MEMRANGE_DESCRIPTION Description;

                if (LogDev != 0 || TagLen != sizeof(ISAPNP_MEMRANGE_DESCRIPTION) ||
                    NumberOfMemRange >= RTL_NUMBER_OF(LogDevice->MemRange))
                {
                    goto SkipTag;
                }

                if (DfState == dfStarted)
                {
                    if (NumberOfDepedentSet >= ISAPNP_MAX_ALTERNATIVES)
                        goto SkipTag;

                    Description = &LogDevice->Alternatives->MemRange[NumberOfDepedentSet];
                }
                else
                {
                    Description = &LogDevice->MemRange[NumberOfMemRange].Description;

                    ++NumberOfMemRange;
                }

                PeekCached(ResourceData, Description, TagLen);
                ResourceData += TagLen;

                DPRINT("Found tag 0x%X (len %d)\n"
                       "  Information 0x%X\n"
                       "  Minimum     0x%X\n"
                       "  Maximum     0x%X\n"
                       "  Alignment   0x%X\n"
                       "  Length      0x%X\n",
                       Tag, TagLen,
                       Description->Information,
                       Description->Minimum,
                       Description->Maximum,
                       Description->Alignment,
                       Description->Length);

                break;
            }

            case ISAPNP_TAG_ANSISTR:
            {
                PSTR End;

                /* Check if the found tag starts before the LOGDEVID tag for LDN 0 */
                if (!(!LogDevice->FriendlyName && (LogDevToRead == 0 || LogDev == 0)))
                    goto SkipTag;

                LogDevice->FriendlyName = ExAllocatePoolWithTag(PagedPool,
                                                                TagLen + sizeof(ANSI_NULL),
                                                                TAG_ISAPNP);
                if (!LogDevice->FriendlyName)
                    return STATUS_INSUFFICIENT_RESOURCES;

                PeekCached(ResourceData, LogDevice->FriendlyName, TagLen);
                ResourceData += TagLen;

                End = LogDevice->FriendlyName + TagLen - 1;
                while (End > LogDevice->FriendlyName && *End == ' ')
                {
                    --End;
                }
                *++End = ANSI_NULL;

                DPRINT("Found tag 0x%X (len %d)\n"
                       "  '%s'\n",
                       Tag, TagLen,
                       LogDevice->FriendlyName);

                break;
            }

            case ISAPNP_TAG_UNICODESTR:
            {
                /*
                 * TODO: Implement
                 * 1) Convert to ANSI
                 * 2) Write to LogDevice->FriendlyName
                 */
                 goto SkipTag;
            }

            case ISAPNP_TAG_MEM32RANGE:
            {
                PISAPNP_MEMRANGE32_DESCRIPTION Description;

                if (LogDev != 0 || TagLen != sizeof(ISAPNP_MEMRANGE32_DESCRIPTION) ||
                    NumberOfMemRange32 >= RTL_NUMBER_OF(LogDevice->MemRange32))
                {
                    goto SkipTag;
                }

                if (DfState == dfStarted)
                {
                    if (NumberOfDepedentSet >= ISAPNP_MAX_ALTERNATIVES)
                        goto SkipTag;

                    Description = &LogDevice->Alternatives->MemRange32[NumberOfDepedentSet];
                }
                else
                {
                    Description = &LogDevice->MemRange32[NumberOfMemRange32].Description;

                    ++NumberOfMemRange32;
                }

                PeekCached(ResourceData, Description, TagLen);
                ResourceData += TagLen;

                DPRINT("Found tag 0x%X (len %d)\n"
                       "  Information 0x%X\n"
                       "  Minimum     0x%08X\n"
                       "  Maximum     0x%08X\n"
                       "  Alignment   0x%08X\n"
                       "  Length      0x%08X\n",
                       Tag, TagLen,
                       Description->Information,
                       Description->Minimum,
                       Description->Maximum,
                       Description->Alignment,
                       Description->Length);

                break;
            }

            case ISAPNP_TAG_FIXEDMEM32RANGE:
            {
                ISAPNP_FIXEDMEMRANGE_DESCRIPTION Temp;
                PISAPNP_MEMRANGE32_DESCRIPTION Description;

                if (LogDev != 0 || TagLen != sizeof(ISAPNP_FIXEDMEMRANGE_DESCRIPTION) ||
                    NumberOfMemRange32 >= RTL_NUMBER_OF(LogDevice->MemRange32))
                {
                    goto SkipTag;
                }

                if (DfState == dfStarted)
                {
                    if (NumberOfDepedentSet >= ISAPNP_MAX_ALTERNATIVES)
                        goto SkipTag;

                    Description = &LogDevice->Alternatives->MemRange32[NumberOfDepedentSet];
                }
                else
                {
                    Description = &LogDevice->MemRange32[NumberOfMemRange32].Description;

                    ++NumberOfMemRange32;
                }

                PeekCached(ResourceData, &Temp, TagLen);
                ResourceData += TagLen;

                Description->Information = Temp.Information;
                Description->Minimum =
                Description->Maximum = Temp.MemoryBase;
                Description->Alignment = 1;
                Description->Length = Temp.Length;

                DPRINT("Found tag 0x%X (len %d)\n"
                       "  Information 0x%X\n"
                       "  MemoryBase  0x%X\n"
                       "  Length      0x%X\n",
                       Tag, TagLen,
                       Temp.Information,
                       Temp.MemoryBase,
                       Temp.Length);

                break;
            }

SkipTag:
            default:
            {
                if (LogDev == 0)
                    DPRINT("Found unknown tag 0x%X (len %d)\n", Tag, TagLen);

                /* We don't want to read informations on this
                 * logical device, or we don't know the tag. */
                ResourceData += TagLen;
                break;
            }
        }
    }
}

static
CODE_SEG("PAGE")
NTSTATUS
ReadCurrentResources(
    _In_ PUCHAR ReadDataPort,
    _Inout_ PISAPNP_LOGICAL_DEVICE LogDevice)
{
    ULONG i;
    BOOLEAN IsUpperLimit;

    PAGED_CODE();

    DPRINT("%s for CSN %lu, LDN %lu\n", __FUNCTION__, LogDevice->CSN, LogDevice->LDN);

    /* If the device is not activated we just report a NULL resourse list */
    if (!(ReadByte(ReadDataPort, ISAPNP_ACTIVATE) & 1))
    {
        LogDevice->Flags &= ~ISAPNP_HAS_RESOURCES;
        return STATUS_UNSUCCESSFUL;
    }

    for (i = 0; i < RTL_NUMBER_OF(LogDevice->Io); i++)
    {
        LogDevice->Io[i].CurrentBase = ReadIoBase(ReadDataPort, i);

        /* The next descriptors are empty */
        if (!LogDevice->Io[i].CurrentBase)
            break;
    }

    for (i = 0; i < RTL_NUMBER_OF(LogDevice->Irq); i++)
    {
        LogDevice->Irq[i].CurrentNo = ReadIrqNo(ReadDataPort, i);

        if (!LogDevice->Irq[i].CurrentNo)
            break;

        LogDevice->Irq[i].CurrentType = ReadIrqType(ReadDataPort, i);
    }

    for (i = 0; i < RTL_NUMBER_OF(LogDevice->Dma); i++)
    {
        LogDevice->Dma[i].CurrentChannel = ReadDmaChannel(ReadDataPort, i);

        if (!LogDevice->Dma[i].CurrentChannel)
            break;
    }

    IsUpperLimit = ReadByte(ReadDataPort, ISAPNP_MEMORYCONTROL) & 1;

    for (i = 0; i < RTL_NUMBER_OF(LogDevice->MemRange); i++)
    {
        /* Handle register gap */
        if (i == 0)
        {
            LogDevice->MemRange[i].CurrentBase = ReadWord(ReadDataPort, 0x40) << 8;

            if (!LogDevice->MemRange[i].CurrentBase)
                break;

            LogDevice->MemRange[i].CurrentLength = ReadWord(ReadDataPort, 0x43) << 8;
        }
        else
        {
            LogDevice->MemRange[i].CurrentBase = ReadMemoryBase(ReadDataPort, i) << 8;

            if (!LogDevice->MemRange[i].CurrentBase)
                break;

            LogDevice->MemRange[i].CurrentLength = ReadMemoryLimit(ReadDataPort, i) << 8;
        }

        if (IsUpperLimit)
        {
            LogDevice->MemRange[i].CurrentLength -= LogDevice->MemRange[i].CurrentBase;
        }
        else
        {
            LogDevice->MemRange[i].CurrentLength =
                ~(LogDevice->MemRange[i].CurrentLength + 1) & 0xFFFFFF;
        }
    }

    IsUpperLimit = ReadByte(ReadDataPort, ISAPNP_MEMORYCONTROL32) & 1;

    for (i = 0; i < RTL_NUMBER_OF(LogDevice->MemRange32); i++)
    {
        /* Handle register gap */
        if (i == 0)
        {
            LogDevice->MemRange32[i].CurrentBase = ReadDoubleWord(ReadDataPort, 0x76);

            if (!LogDevice->MemRange32[i].CurrentBase)
                break;

            LogDevice->MemRange32[i].CurrentLength = ReadDoubleWord(ReadDataPort, 0x7B);
        }
        else
        {
            LogDevice->MemRange32[i].CurrentBase = ReadMemoryBase32(ReadDataPort, i);

            if (!LogDevice->MemRange32[i].CurrentBase)
                break;

            LogDevice->MemRange32[i].CurrentLength = ReadMemoryLimit32(ReadDataPort, i);
        }

        if (IsUpperLimit)
        {
            LogDevice->MemRange32[i].CurrentLength -= LogDevice->MemRange32[i].CurrentBase;
        }
        else
        {
            LogDevice->MemRange32[i].CurrentLength =
                ~(LogDevice->MemRange32[i].CurrentLength + 1) & 0xFFFFFF;
        }
    }

    LogDevice->Flags |= ISAPNP_HAS_RESOURCES;
    return STATUS_SUCCESS;
}

static
CODE_SEG("PAGE")
INT
TryIsolate(
    _In_ PUCHAR ReadDataPort)
{
    ISAPNP_IDENTIFIER Identifier;
    USHORT i, j;
    BOOLEAN Seen55aa, SeenLife;
    INT Csn = 0;
    USHORT Byte, Data;

    PAGED_CODE();

    DPRINT("Setting read data port: 0x%p\n", ReadDataPort);

    WaitForKey();
    SendKey();

    ResetCsn();
    HwDelay();
    HwDelay();

    WaitForKey();
    SendKey();
    Wake(0x00);

    SetReadDataPort(ReadDataPort);
    HwDelay();

    while (TRUE)
    {
        EnterIsolationState();
        HwDelay();

        RtlZeroMemory(&Identifier, sizeof(Identifier));

        Seen55aa = SeenLife = FALSE;
        for (i = 0; i < 9; i++)
        {
            Byte = 0;
            for (j = 0; j < 8; j++)
            {
                Data = ReadData(ReadDataPort);
                HwDelay();
                Data = ((Data << 8) | ReadData(ReadDataPort));
                HwDelay();
                Byte >>= 1;

                if (Data != 0xFFFF)
                {
                    SeenLife = TRUE;
                    if (Data == 0x55AA)
                    {
                        Byte |= 0x80;
                        Seen55aa = TRUE;
                    }
                }
            }
            *(((PUCHAR)&Identifier) + i) = Byte;
        }

        if (!Seen55aa)
        {
            if (Csn)
            {
                DPRINT("Found no more cards\n");
            }
            else
            {
                if (SeenLife)
                {
                    DPRINT("Saw life but no cards, trying new read port\n");
                    Csn = -1;
                }
                else
                {
                    DPRINT("Saw no sign of life, abandoning isolation\n");
                }
            }
            break;
        }

        if (Identifier.Checksum != IsaPnpChecksum(&Identifier))
        {
            DPRINT("Bad checksum, trying next read data port\n");
            Csn = -1;
            break;
        }

        Csn++;

        WriteCsn(Csn);
        HwDelay();

        Wake(0x00);
        HwDelay();
    }

    WaitForKey();

    if (Csn > 0)
    {
        DPRINT("Found %d cards at read port 0x%p\n", Csn, ReadDataPort);
    }

    return Csn;
}

static
VOID
DeviceActivation(
    _In_ PUCHAR ReadDataPort,
    _In_ PISAPNP_LOGICAL_DEVICE IsaDevice,
    _In_ BOOLEAN Activate)
{
    WaitForKey();
    SendKey();
    Wake(IsaDevice->CSN);

    if (Activate)
        ActivateDevice(ReadDataPort, IsaDevice->LDN);
    else
        DeactivateDevice(IsaDevice->LDN);

    HwDelay();

    WaitForKey();
}

_Requires_lock_held_(*FdoExt->DeviceSyncEvent)
static
CODE_SEG("PAGE")
NTSTATUS
ProbeIsaPnpBus(
    _In_ PISAPNP_FDO_EXTENSION FdoExt)
{
    PISAPNP_LOGICAL_DEVICE LogDevice;
    USHORT Csn;
    PLIST_ENTRY Entry;
    PUCHAR ResourceData;

    PAGED_CODE();
    ASSERT(FdoExt->ReadDataPort);

    DPRINT("%s for read port 0x%p\n", __FUNCTION__, FdoExt->ReadDataPort);

    ResourceData = ExAllocatePoolWithTag(PagedPool, ISAPNP_MAX_RESOURCEDATA, TAG_ISAPNP);
    if (!ResourceData)
    {
        DPRINT1("Failed to allocate memory for cache data\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Entry = FdoExt->DeviceListHead.Flink;
         Entry != &FdoExt->DeviceListHead;
         Entry = Entry->Flink)
    {
        LogDevice = CONTAINING_RECORD(Entry, ISAPNP_LOGICAL_DEVICE, DeviceLink);

        LogDevice->Flags &= ~ISAPNP_PRESENT;
    }

    WaitForKey();
    SendKey();

    for (Csn = 1; Csn <= FdoExt->Cards; Csn++)
    {
        NTSTATUS Status;
        UCHAR Temp[3];
        ISAPNP_IDENTIFIER Identifier;
        USHORT LogDev, MaxLogDev;

        Wake(Csn);

        Peek(FdoExt->ReadDataPort, &Identifier, sizeof(Identifier));

        IsaPnpExtractAscii(Temp, Identifier.VendorId);

        Status = ReadTags(FdoExt->ReadDataPort, ResourceData, ISAPNP_MAX_RESOURCEDATA, &MaxLogDev);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Failed to read tags with status 0x%08lx, CSN %lu\n", Status, Csn);
            continue;
        }

        for (LogDev = 0; LogDev < MaxLogDev; LogDev++)
        {
            for (Entry = FdoExt->DeviceListHead.Flink;
                 Entry != &FdoExt->DeviceListHead;
                 Entry = Entry->Flink)
            {
                LogDevice = CONTAINING_RECORD(Entry, ISAPNP_LOGICAL_DEVICE, DeviceLink);

                /* This logical device has already been enumerated */
                if ((LogDevice->SerialNumber == Identifier.Serial) &&
                    (RtlCompareMemory(LogDevice->VendorId, Temp, 3) == 3) &&
                    (LogDevice->ProdId == RtlUshortByteSwap(Identifier.ProdId)) &&
                    (LogDevice->LDN == LogDev))
                {
                    LogDevice->Flags |= ISAPNP_PRESENT;

                    /* Assign a new CSN */
                    LogDevice->CSN = Csn;

                    DPRINT("Skip CSN %lu, LDN %lu\n", LogDevice->CSN, LogDevice->LDN);
                    goto Skip;
                }
            }

            LogDevice = ExAllocatePoolZero(NonPagedPool, sizeof(ISAPNP_LOGICAL_DEVICE), TAG_ISAPNP);
            if (!LogDevice)
            {
                ExFreePoolWithTag(ResourceData, TAG_ISAPNP);
                return STATUS_NO_MEMORY;
            }

            InitializeListHead(&LogDevice->CompatibleIdList);

            LogDevice->CSN = Csn;
            LogDevice->LDN = LogDev;

            Status = ParseTags(ResourceData, LogDev, LogDevice);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Failed to parse tags with status 0x%08lx, CSN %lu, LDN %lu\n",
                        Status, LogDevice->CSN, LogDevice->LDN);
                ExFreePoolWithTag(LogDevice, TAG_ISAPNP);
                goto Skip;
            }

            WriteLogicalDeviceNumber(LogDev);

            Status = ReadCurrentResources(FdoExt->ReadDataPort, LogDevice);
            if (!NT_SUCCESS(Status))
            {
                DPRINT("Unable to read resources with status 0x%08lx\n", Status);
            }

            IsaPnpExtractAscii(LogDevice->VendorId, Identifier.VendorId);
            IsaPnpExtractAscii(LogDevice->LogVendorId, LogDevice->LogDevId.VendorId);

            LogDevice->ProdId = RtlUshortByteSwap(Identifier.ProdId);
            LogDevice->LogProdId = RtlUshortByteSwap(LogDevice->LogDevId.ProdId);
            LogDevice->SerialNumber = Identifier.Serial;

            DPRINT("Detected ISA PnP device - VID: '%3s' PID: 0x%04x SN: 0x%08X\n",
                   LogDevice->VendorId, LogDevice->ProdId, LogDevice->SerialNumber);

            LogDevice->Flags |= ISAPNP_PRESENT;

            InsertTailList(&FdoExt->DeviceListHead, &LogDevice->DeviceLink);
            FdoExt->DeviceCount++;

Skip:
            /* Now we wait for the start device IRP */
            DeactivateDevice(LogDevice->LDN);
        }
    }

    ExFreePoolWithTag(ResourceData, TAG_ISAPNP);

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
ULONG
IsaHwTryReadDataPort(
    _In_ PUCHAR ReadDataPort)
{
    return TryIsolate(ReadDataPort);
}

CODE_SEG("PAGE")
NTSTATUS
IsaHwConfigureDevice(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _In_ PISAPNP_LOGICAL_DEVICE LogicalDevice,
    _In_ PCM_RESOURCE_LIST Resources)
{
    PAGED_CODE();

    if (!Resources)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* FIXME */
    UNREFERENCED_PARAMETER(FdoExt);
    UNREFERENCED_PARAMETER(LogicalDevice);
    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
IsaHwActivateDevice(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _In_ PISAPNP_LOGICAL_DEVICE LogicalDevice)
{
    DeviceActivation(FdoExt->ReadDataPort, LogicalDevice, TRUE);

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
IsaHwDeactivateDevice(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _In_ PISAPNP_LOGICAL_DEVICE LogicalDevice)
{
    DeviceActivation(FdoExt->ReadDataPort, LogicalDevice, FALSE);

    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
IsaHwFillDeviceList(
    _In_ PISAPNP_FDO_EXTENSION FdoExt)
{
    return ProbeIsaPnpBus(FdoExt);
}
