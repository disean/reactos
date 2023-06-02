/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     OLPC XO-1 Hardware Detection
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

/* GLOBALS ********************************************************************/

#define TAG_HW_ID     'WHLO'

/* FUNCTIONS ******************************************************************/

static
PSTR
OlpcBuildSystemIdentifier(VOID)
{
    ULONG Length1, Length2;
    PSTR Buffer;
    OFW_PHANDLE RootNode;
#define BUFFER_SIZE    128

    Buffer = FrLdrHeapAlloc(BUFFER_SIZE, TAG_HW_ID);
    if (!Buffer)
    {
        ERR("No memory\n");
        return NULL;
    }

    OFwFindDevice("/", &RootNode);

    if (!OFwGetProperty(RootNode, "architecture", &Buffer, BUFFER_SIZE, &Length1))
        return NULL;
    Buffer[Length1 - 1] = ' ';

    if (!OFwGetProperty(RootNode, "model", &Buffer[Length1], BUFFER_SIZE - Length1, &Length2))
        return NULL;
    Buffer[Length1 + Length2] = ANSI_NULL;

    return Buffer;
}

PCONFIGURATION_COMPONENT_DATA
OlpcHwDetect(VOID)
{
    PCONFIGURATION_COMPONENT_DATA SystemKey;
    PSTR Identifier;

    TRACE("DetectHardware()\n");

    Identifier = OlpcBuildSystemIdentifier();
    if (!Identifier)
        return NULL;

    FldrCreateSystemKey(&SystemKey, Identifier);
    FrLdrHeapFree(Identifier, TAG_HW_ID);

    TRACE("DetectHardware() Done\n");
    return SystemKey;
}
