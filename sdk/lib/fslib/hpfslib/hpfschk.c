/*
 * PROJECT:     ReactOS HPFS filesystem library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Filesystem ChkDsk support functions
 * COPYRIGHT:   Copyright 2022 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include "hpfslib.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
HpfsChkdsk(
    IN PCUNICODE_STRING DriveRoot,
    IN PFMIFSCALLBACK Callback,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PVOID pUnknown1,
    IN PVOID pUnknown2,
    IN PVOID pUnknown3,
    IN PVOID pUnknown4,
    IN PULONG ExitStatus)
{
    UNIMPLEMENTED;

    UNREFERENCED_PARAMETER(pUnknown1);
    UNREFERENCED_PARAMETER(pUnknown2);
    UNREFERENCED_PARAMETER(pUnknown3);
    UNREFERENCED_PARAMETER(pUnknown4);

    *ExitStatus = (ULONG)STATUS_SUCCESS;
    return TRUE;
}
