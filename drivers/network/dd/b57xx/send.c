/*
 * PROJECT:     ReactOS Broadcom NetXtreme Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Packet sending
 * COPYRIGHT:   Copyright 2021-2022 Scott Maday <coldasdryice1@gmail.com>
 */

#include "nic.h"

#define NDEBUG
#include <debug.h>

static
_Check_return_
NDIS_STATUS
NICTransmitPacket(
    _In_ PB57XX_ADAPTER Adapter,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG Length,
    _In_ USHORT Flags)
{
    ULONG Index = Adapter->SendProducer[0].ProducerIndex;

    if (Adapter->SendProducer[0].RingFull == TRUE)
    {
        return NDIS_STATUS_RESOURCES;
    }

    Adapter->SendProducer[0].pRing[Index].Length = Length;
    Adapter->SendProducer[0].pRing[Index].Flags = Flags;
    B57XXConvertAddress(&Adapter->SendProducer[0].pRing[Index].HostAddress, &PhysicalAddress);

    Index = (Index + 1) % Adapter->SendProducer[0].Count;
    Adapter->SendProducer[0].ProducerIndex = Index;
    B57XXWriteMailbox(Adapter, B57XX_MBOX_TXP_HOST_RING_INDEX1, Index);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
MiniportSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_PACKET Packet,
    _In_ UINT Flags)
{
    PB57XX_ADAPTER Adapter = MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_PENDING;
    PSCATTER_GATHER_LIST SGList = NDIS_PER_PACKET_INFO_FROM_PACKET(Packet,
                                                                   ScatterGatherListPacketInfo);
    LONG BusyDescriptors = Adapter->SendProducer[0].ProducerIndex -
                           Adapter->SendProducer[0].ConsumerIndex;

    ASSERT(SGList != NULL);
    ASSERT(SGList->NumberOfElements >= 1);

    NdisDprAcquireSpinLock(&Adapter->SendLock);

    if (BusyDescriptors < 0)
    {
        BusyDescriptors += Adapter->SendProducer[0].Count;
    }

    if (Adapter->SendProducer[0].RingFull == TRUE ||
        BusyDescriptors + SGList->NumberOfElements > Adapter->SendProducer[0].Count)
    {
        /* Force NDIS to resubmit the send */
        Status = NDIS_STATUS_RESOURCES;
        goto Exit;
    }

    for (UINT i = 0; i < SGList->NumberOfElements; i++)
    {
        ASSERT(SGList->Elements[i].Length <= Adapter->MaxFrameSize);

        Adapter->SendProducer[0].pPacketList[Adapter->SendProducer[0].ProducerIndex] = Packet;

        NICTransmitPacket(Adapter,
                          SGList->Elements[i].Address,
                          SGList->Elements[i].Length,
                          i == SGList->NumberOfElements - 1 ? B57XX_SBD_PACKET_END : 0);
    }

    if (Adapter->SendProducer[0].ProducerIndex == Adapter->SendProducer[0].ConsumerIndex)
    {
        NDIS_MinDbgPrint("Send ring is full\n");
        Adapter->SendProducer[0].RingFull = TRUE;
    }

Exit:
    NdisDprReleaseSpinLock(&Adapter->SendLock);

    return Status;
}
