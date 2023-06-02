/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Open Firmware Client Interface ABI
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov (di.sean@protonmail.com)
 */

#include <asm.inc>

.code32

/*
 * ULONG OFwCallClientInterfaceHandler(PVOID Arguments);
 */
PUBLIC _OFwCallClientInterfaceHandler@4
_OFwCallClientInterfaceHandler@4:
    mov eax, [esp + 4]

    call dword ptr ds:[_OFwClientInterfaceHandler]

    ret 4

PUBLIC _OFwClientInterfaceHandler
_OFwClientInterfaceHandler:
    .long 0

END
