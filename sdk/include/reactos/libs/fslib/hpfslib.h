/*
 * PROJECT:     ReactOS HPFS filesystem library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Public header file
 * COPYRIGHT:   Copyright 2022 Dmitry Borisov (di.sean@protonmail.com)
 */

#ifndef __HPFSLIB_H
#define __HPFSLIB_H

#include <fmifs/fmifs.h>

BOOLEAN
NTAPI
HpfsChkdsk(
    IN PUNICODE_STRING DriveRoot,
    IN PFMIFSCALLBACK Callback,
    IN BOOLEAN FixErrors,
    IN BOOLEAN Verbose,
    IN BOOLEAN CheckOnlyIfDirty,
    IN BOOLEAN ScanDrive,
    IN PVOID pUnknown1,
    IN PVOID pUnknown2,
    IN PVOID pUnknown3,
    IN PVOID pUnknown4,
    IN PULONG ExitStatus);

BOOLEAN
NTAPI
HpfsFormat(
    IN PUNICODE_STRING DriveRoot,
    IN PFMIFSCALLBACK Callback,
    IN BOOLEAN QuickFormat,
    IN BOOLEAN BackwardCompatible,
    IN MEDIA_TYPE MediaType,
    IN PUNICODE_STRING Label,
    IN ULONG ClusterSize);

#endif /* __HPFSLIB_H */
