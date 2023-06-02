/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Open Firmware Console I/O Routines
 * COPYRIGHT:   Copyright 2007-2009 Aleksey Bragin (aleksey@reactos.org)
 *              Copyright 2023 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

/* GLOBALS ********************************************************************/

#define KEY_NO_EVENT    0

static UCHAR OFwpConsEolBuffer[2] = { '\r', '\n' };
static UCHAR OFwpConsTabBuffer[8] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };

static ULONG OFwpConsKey = KEY_NO_EVENT;
static BOOLEAN OFwpConsReturnExtendedKey = TRUE;

/* FUNCTIONS ******************************************************************/

VOID
OFwConsPutChar(
    int Ch)
{
    PVOID Buffer;
    ULONG Length;

    switch (Ch)
    {
        case '\n':
            Buffer = OFwpConsEolBuffer;
            Length = sizeof(OFwpConsEolBuffer);
            break;

        case '\t':
            Buffer = OFwpConsTabBuffer;
            Length = sizeof(OFwpConsTabBuffer);
            break;

        default:
            Buffer = &Ch;
            Length = 1;
            break;
    }

    OFwWrite(OFwpStdOutHandle, Buffer, Length);
}

BOOLEAN
OFwConsKbHit(VOID)
{
    OFwpConsKey = KEY_NO_EVENT;

    if (!OFwRead(OFwpStdInHandle, &OFwpConsKey, sizeof(OFwpConsKey)))
        return FALSE;

    return (OFwpConsKey != KEY_NO_EVENT);
}

int
OFwConsGetCh(VOID)
{
    ULONG Ch;

    /* Check for a pending key event */
    Ch = OFwpConsKey;
    if (Ch != KEY_NO_EVENT)
    {
        goto ReturnChar;
    }

    /* Poll for a key press */
    while (TRUE)
    {
        Ch = KEY_NO_EVENT;

        if (!OFwRead(OFwpStdInHandle, &Ch, sizeof(Ch)))
            break;

        if (Ch != KEY_NO_EVENT)
            break;

        OlpcHwIdle();
    }
    OFwpConsKey = Ch;

ReturnChar:
    /* "Extended" key */
    if (Ch & 0xFFFFFF00)
    {
        if (OFwpConsReturnExtendedKey)
        {
            OFwpConsReturnExtendedKey = FALSE;
            return KEY_EXTENDED;
        }

        OFwpConsReturnExtendedKey = TRUE;
        OFwpConsKey = KEY_NO_EVENT;
    }

    /* FIXME: HACKish */
    switch (Ch)
    {
        case 0xD:      return KEY_ENTER;
        case 0x8:      return KEY_BACKSPACE;
        case 0x7F:     return KEY_DELETE;
        case 0x20:     return KEY_SPACE;
        case 0x1B:     return KEY_ESC;
        case 0x504F9B: return KEY_F1;
        case 0x514F9B: return KEY_F2;
        case 0x574F9B: return KEY_F3;
        case 0x784F9B: return KEY_F4;
        case 0x744F9B: return KEY_F5;
        case 0x754F9B: return KEY_F6;
        case 0x714F9B: return KEY_F7;
        case 0x724F9B: return KEY_F8;
        case 0x704F9B: return KEY_F9;
        case 0x4D4F9B: return KEY_F10;
        case 0x4B9B:   return KEY_END;
        case 0x489B:   return KEY_HOME;
        case 0x419B:   return KEY_UP;
        case 0x429B:   return KEY_DOWN;
        case 0x449B:   return KEY_LEFT;
        case 0x439B:   return KEY_RIGHT;

        default:
            break;
    }

    return Ch;
}
