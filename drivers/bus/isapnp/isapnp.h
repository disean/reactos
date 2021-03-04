#ifndef _ISAPNP_PCH_
#define _ISAPNP_PCH_

#include <wdm.h>
#include <ntstrsafe.h>
#include <section_attribs.h>
#include <isapnphw.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAG_ISAPNP 'pasI'

#define ISAPNP_MAX_RESOURCEDATA 0x1000

/** @brief Maximum number of Start DF tags supported by the driver. */
#define ISAPNP_MAX_ALTERNATIVES 8

typedef enum
{
    dsStopped,
    dsStarted
} ISAPNP_DEVICE_STATE;

typedef struct _ISAPNP_IO
{
    USHORT CurrentBase;
    ISAPNP_IO_DESCRIPTION Description;
} ISAPNP_IO, *PISAPNP_IO;

typedef struct _ISAPNP_IRQ
{
    UCHAR CurrentNo;
    UCHAR CurrentType;
    ISAPNP_IRQ_DESCRIPTION Description;
} ISAPNP_IRQ, *PISAPNP_IRQ;

typedef struct _ISAPNP_DMA
{
    UCHAR CurrentChannel;
    ISAPNP_DMA_DESCRIPTION Description;
} ISAPNP_DMA, *PISAPNP_DMA;

typedef struct _ISAPNP_MEMRANGE
{
    ULONG CurrentBase;
    ULONG CurrentLength;
    ISAPNP_MEMRANGE_DESCRIPTION Description;
} ISAPNP_MEMRANGE, *PISAPNP_MEMRANGE;

typedef struct _ISAPNP_MEMRANGE32
{
    ULONG CurrentBase;
    ULONG CurrentLength;
    ISAPNP_MEMRANGE32_DESCRIPTION Description;
} ISAPNP_MEMRANGE32, *PISAPNP_MEMRANGE32;

typedef struct _ISAPNP_COMPATIBLE_ID_ENTRY
{
    UCHAR VendorId[3];
    USHORT ProdId;
    LIST_ENTRY IdLink;
} ISAPNP_COMPATIBLE_ID_ENTRY, *PISAPNP_COMPATIBLE_ID_ENTRY;

typedef struct _ISAPNP_ALTERNATIVES
{
    ISAPNP_IO_DESCRIPTION Io[ISAPNP_MAX_ALTERNATIVES];
    ISAPNP_IRQ_DESCRIPTION Irq[ISAPNP_MAX_ALTERNATIVES];
    ISAPNP_DMA_DESCRIPTION Dma[ISAPNP_MAX_ALTERNATIVES];
    ISAPNP_MEMRANGE_DESCRIPTION MemRange[ISAPNP_MAX_ALTERNATIVES];
    ISAPNP_MEMRANGE32_DESCRIPTION MemRange32[ISAPNP_MAX_ALTERNATIVES];
    UCHAR Priority[ISAPNP_MAX_ALTERNATIVES];

    _Field_range_(0, ISAPNP_MAX_ALTERNATIVES)
    UCHAR Count;
} ISAPNP_ALTERNATIVES, *PISAPNP_ALTERNATIVES;

typedef struct _ISAPNP_LOGICAL_DEVICE
{
    PDEVICE_OBJECT Pdo;

    /**
     * @brief The CSN data.
     * @{
     */
    UCHAR VendorId[3];
    USHORT ProdId;
    ULONG SerialNumber;
    /**@}*/

    /**
     * @brief The LDN data.
     * @{
     */
    UCHAR LogVendorId[3];
    USHORT LogProdId;
    ISAPNP_LOGDEVID LogDevId;
    LIST_ENTRY CompatibleIdList;
    PSTR FriendlyName;
    PISAPNP_ALTERNATIVES Alternatives;

    ISAPNP_IO Io[8];
    ISAPNP_IRQ Irq[2];
    ISAPNP_DMA Dma[2];
    ISAPNP_MEMRANGE MemRange[4];
    ISAPNP_MEMRANGE32 MemRange32[4];
    /**@}*/

    UCHAR CSN;
    UCHAR LDN;

    ULONG Flags;
#define ISAPNP_PRESENT       0x00000001 /**< Cleared when the device is physically removed. */
#define ISAPNP_HAS_RESOURCES 0x00000002 /**< Cleared when the device has no assigned resources. */

    LIST_ENTRY DeviceLink;
} ISAPNP_LOGICAL_DEVICE, *PISAPNP_LOGICAL_DEVICE;

typedef struct _ISAPNP_COMMON_EXTENSION
{
    PDEVICE_OBJECT Self;
    BOOLEAN IsFdo;
    ISAPNP_DEVICE_STATE State;
} ISAPNP_COMMON_EXTENSION, *PISAPNP_COMMON_EXTENSION;

typedef struct _ISAPNP_FDO_EXTENSION
{
    ISAPNP_COMMON_EXTENSION Common;
    PDEVICE_OBJECT Ldo;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT ReadPortPdo;
    ULONG BusNumber;

    _Has_lock_kind_(_Lock_kind_event_)
    KEVENT DeviceSyncEvent;

    _Guarded_by_(DeviceSyncEvent)
    LIST_ENTRY DeviceListHead;

    _Guarded_by_(DeviceSyncEvent)
    ULONG DeviceCount;

    PDRIVER_OBJECT DriverObject;
    PUCHAR ReadDataPort;
    ULONG Cards;
    LIST_ENTRY BusLink;
} ISAPNP_FDO_EXTENSION, *PISAPNP_FDO_EXTENSION;

typedef struct _ISAPNP_PDO_EXTENSION
{
    ISAPNP_COMMON_EXTENSION Common;
    PISAPNP_LOGICAL_DEVICE IsaPnpDevice;
    PISAPNP_FDO_EXTENSION FdoExt;
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList;
    PCM_RESOURCE_LIST ResourceList;
    ULONG ResourceListSize;

    ULONG Flags;
#define ISAPNP_ENUMERATED               0x00000001 /**< Whether the device has been reported to the PnP manager. */
#define ISAPNP_READ_PORT_NEED_REBALANCE 0x00000002

    _Interlocked_
    volatile LONG SpecialFiles;
} ISAPNP_PDO_EXTENSION, *PISAPNP_PDO_EXTENSION;

extern BOOLEAN ReadPortCreated;
extern KEVENT BusSyncEvent;
extern LIST_ENTRY BusListHead;

_Acquires_lock_(BusSyncEvent)
FORCEINLINE
VOID
IsaPnpAcquireBusDataLock(VOID)
{
    KeWaitForSingleObject(&BusSyncEvent, Executive, KernelMode, FALSE, NULL);
}

_Releases_lock_(BusSyncEvent)
FORCEINLINE
VOID
IsaPnpReleaseBusDataLock(VOID)
{
    KeSetEvent(&BusSyncEvent, IO_NO_INCREMENT, FALSE);
}

_Acquires_lock_(*FdoExt->SyncEvent)
FORCEINLINE
VOID
IsaPnpAcquireDeviceDataLock(
    _In_ PISAPNP_FDO_EXTENSION FdoExt)
{
    KeWaitForSingleObject(&FdoExt->DeviceSyncEvent, Executive, KernelMode, FALSE, NULL);
}

_Releases_lock_(*FdoExt->DeviceSyncEvent)
FORCEINLINE
VOID
IsaPnpReleaseDeviceDataLock(
    _In_ PISAPNP_FDO_EXTENSION FdoExt)
{
    KeSetEvent(&FdoExt->DeviceSyncEvent, IO_NO_INCREMENT, FALSE);
}

/* isapnp.c */

NTSTATUS
IsaPnpCreateReadPortDORequirements(
    _In_ PISAPNP_PDO_EXTENSION PdoExt,
    _In_opt_ USHORT SelectedPort);

VOID
IsaPnpRemoveReadPortDO(
    _In_ PDEVICE_OBJECT Pdo);

NTSTATUS
IsaPnpFillDeviceRelations(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _Inout_ PIRP Irp,
    _In_ BOOLEAN IncludeDataPort);

DRIVER_INITIALIZE DriverEntry;

/* fdo.c */
NTSTATUS
IsaFdoPnp(
    _In_ PISAPNP_FDO_EXTENSION FdoExt,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp);

/* pdo.c */
NTSTATUS
IsaPdoPnp(
    _In_ PISAPNP_PDO_EXTENSION PdoDeviceExtension,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp);

VOID
IsaPnpRemoveLogicalDevice(
    _In_ PDEVICE_OBJECT Pdo);

/* hardware.c */
ULONG
IsaHwTryReadDataPort(
    _In_ PUCHAR ReadDataPort);

NTSTATUS
IsaHwFillDeviceList(
    _In_ PISAPNP_FDO_EXTENSION FdoExt);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
IsaHwDeactivateDevice(
    _In_ PISAPNP_LOGICAL_DEVICE LogicalDevice);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
IsaHwActivateDevice(
    _In_ PISAPNP_LOGICAL_DEVICE LogicalDevice);

#ifdef __cplusplus
}
#endif

#endif /* _ISAPNP_PCH_ */
