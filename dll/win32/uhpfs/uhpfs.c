/*
 * PROJECT:     HPFS File System Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DLL initialisation
 * COPYRIGHT:   Copyright 2022 Dmitry Borisov (di.sean@protonmail.com)
 */

#include <windef.h>

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE hinstDLL,
    _In_ DWORD dwReason,
    _In_ LPVOID lpvReserved)
{
    UNREFERENCED_PARAMETER(hinstDLL);
    UNREFERENCED_PARAMETER(dwReason);
    UNREFERENCED_PARAMETER(lpvReserved);

    return TRUE;
}
