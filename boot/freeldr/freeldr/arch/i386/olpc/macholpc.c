/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware-specific routines for OLPC XO-1
 * COPYRIGHT:   Copyright 2007-2009 Aleksey Bragin (aleksey@reactos.org)
 *              Copyright 2023 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

/* GLOBALS ********************************************************************/


/* FUNCTIONS ******************************************************************/


/* FUNCTIONS ******************************************************************/

VOID
OlpcBeep(VOID)
{
    /* Not supported by hardware? */
}

VOID
OlpcGetExtendedBIOSData(
    PULONG ExtendedBIOSDataArea,
    PULONG ExtendedBIOSDataSize)
{
    /* Not supported by hardware */
}

UCHAR
OlpcGetFloppyCount(VOID)
{
    return 0;
}

BOOLEAN
OlpcInitializeBootDevices(VOID)
{
    return PcInitializeBootDevices();
}

VOID
OlpcPrepareForReactOS(VOID)
{

}

VOID
OlpcHwIdle(VOID)
{
    /* TODO */
}

VOID
__cdecl
DiskStopFloppyMotor(VOID)
{

}

VOID
__cdecl
ChainLoadBiosBootSectorCode(
    IN UCHAR BootDrive OPTIONAL,
    IN ULONG BootPartition OPTIONAL)
{

}

#define ACPI_IO_BASE              0x1840
#define ACPI_PM_TIMER             0x10

VOID
StallExecutionProcessor(
    ULONG Microseconds)
{
    ULONG64 TicksNeeded, TicksElapsed;
    ULONG StartValue, CurrentValue;

    /*
     * The ACPI PM timer runs at 3.579545 MHz. To avoid a 64-bit division we approximate
     * 3579545/1000000 as 3665/1024. This introduces an error of only +0.000123%.
     */
    TicksNeeded = ((ULONG64)Microseconds * 3665) >> 10;
    TicksElapsed = 0;

    StartValue = READ_PORT_ULONG((PVOID)((ULONG_PTR)ACPI_IO_BASE + ACPI_PM_TIMER));

    while (TRUE)
    {
        CurrentValue = READ_PORT_ULONG((PVOID)((ULONG_PTR)ACPI_IO_BASE + ACPI_PM_TIMER));

        /* This is a 32-bit counter, so no handling is necessary across a rollover */
        TicksElapsed += CurrentValue - StartValue;

        StartValue = CurrentValue;

        if (TicksElapsed >= TicksNeeded)
            break;
    }
}

VOID
GetHarddiskInformation(
    UCHAR DriveNumber)
{
    ERR("GetHarddiskInformation\n");
}

LONG
DiskReportError(
    BOOLEAN bShowError)
{
    return 0;
}

VOID
FrLdrCheckCpuCompatibility(VOID)
{

}

VOID
__cdecl
Reboot(VOID)
{
    OFwInterpret("reset-all", NULL);

    while (TRUE)
    {
        NOTHING;
    }
}

VOID
MachInit(
    const char* CmdLine)
{
    /* Setup the vtbl */
    RtlZeroMemory(&MachVtbl, sizeof(MachVtbl));
    MachVtbl.ConsPutChar                = OFwConsPutChar;
    MachVtbl.ConsKbHit                  = OFwConsKbHit;
    MachVtbl.ConsGetCh                  = OFwConsGetCh;
    MachVtbl.Beep                       = OlpcBeep;
    MachVtbl.PrepareForReactOS          = OlpcPrepareForReactOS;
    MachVtbl.GetMemoryMap               = OFwMemGetMemoryMap;
    MachVtbl.GetExtendedBIOSData        = OlpcGetExtendedBIOSData;
    MachVtbl.GetFloppyCount             = OlpcGetFloppyCount;
    MachVtbl.DiskReadLogicalSectors     = OlpcDiskReadLogicalSectors;
    MachVtbl.DiskGetDriveGeometry       = OlpcDiskGetDriveGeometry;
    MachVtbl.DiskGetCacheableBlockCount = OlpcDiskGetCacheableBlockCount;
    MachVtbl.GetTime                    = XboxGetTime;
    MachVtbl.InitializeBootDevices      = OlpcInitializeBootDevices;
    MachVtbl.HwDetect                   = OlpcHwDetect;
    MachVtbl.HwIdle                     = OlpcHwIdle;

#if defined(_MSC_VER)
    OFwConsPutChar('M');
    OFwConsPutChar('S');
    OFwConsPutChar('V');
    OFwConsPutChar('C');
#else
    OFwConsPutChar('G');
    OFwConsPutChar('C');
    OFwConsPutChar('C');
#endif
    OFwConsPutChar('\n');

    if (!OFwVideoInit())
    {
        OFwConsPutChar('E');
        OFwConsPutChar('R');
        OFwConsPutChar('R');
        OFwConsPutChar('\n');

        while(1) {};
    }

    // HACK
    FrldrBootDrive = 0x80;
    FrldrBootPartition = 1;
}
