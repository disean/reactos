/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Open Firmware Video Support
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>

/* GLOBALS ********************************************************************/

UCHAR MachDefaultTextColor = COLOR_GRAY;

static ULONG VidpScreenWidth;
static ULONG VidpScreenHeight;
static ULONG VidpScreenStride;
static ULONG VidpColorMode;
static ULONG VidpDepth;
static ULONG VidpBytesPerPixel;
static PVOID VidpBaseAddress;
static BOOLEAN VidpIsPaletteFixed;

/* Number of bytes per character in ScreenMemory[] */
#define FREELDR_CHAR_SIZE  2

#define ATT_FG_COLOR(Attr)   ((Attr) & 0x0F)
#define ATT_BG_COLOR(Attr)   (((Attr) >> 4) & 0x0F)

#if defined(SARCH_OLPC)
static PUCHAR VidpFontBase;
static ULONG VidpFontCharWidth;
static ULONG VidpFontCharHeight;
static ULONG VidpFontCharStride;
static ULONG VidpFontCharSize;
#define CHAR_WIDTH     VidpFontCharWidth
#define CHAR_HEIGHT    VidpFontCharHeight
#define CHAR_STRIDE    VidpFontCharStride
#else
#define CHAR_WIDTH     8
#define CHAR_HEIGHT    16
#define CHAR_STRIDE    1
extern UCHAR BitmapFont8x16[];
#endif

/* FUNCTIONS ******************************************************************/

static
ULONG
OFwpVideoAttrToSingleColor(
    _In_ ULONG Attr)
{
    static const ULONG ColorMask[5][7] =
    {
        /* 8-bpp, CGA palette */
        {
            0x00000000,             /* A */
            0x00000001, 0x00000009, /* B */
            0x00000002, 0x0000000A, /* G */
            0x00000004, 0x0000000C, /* R */
        },
        /* 555 */
        {
            0x00000000,             /* A */
            0x0000000F, 0x0000001F, /* B */
            0x000001E0, 0x000003E0, /* G */
            0x00003C00, 0x00007C00, /* R */
        },
        /* 565 */
        {
            0x00000000,             /* A */
            0x0000000F, 0x0000001F, /* B */
            0x000003E0, 0x000007E0, /* G */
            0x00007800, 0x0000F800, /* R */
        },
        /* 888 */
        {
            0x00000000,             /* A */
            0x0000007F, 0x000000FF, /* B */
            0x00007F00, 0x0000FF00, /* G */
            0x007F0000, 0x00FF0000, /* R */
        },
        /* 8888 */
        {
            0xFF000000,             /* A */
            0x0000007F, 0x000000FF, /* B */
            0x00007F00, 0x0000FF00, /* G */
            0x007F0000, 0x00FF0000, /* R */
        },
    };
    ULONG Color;
    ULONG Intensity = (Attr & 0x08) >> 3;

    Color = ColorMask[VidpColorMode][0];
    if (Attr & COLOR_BLUE)
        Color |= ColorMask[VidpColorMode][1 + Intensity];
    if (Attr & COLOR_GREEN)
        Color |= ColorMask[VidpColorMode][3 + Intensity];
    if (Attr & COLOR_RED)
        Color |= ColorMask[VidpColorMode][5 + Intensity];

    return Color;
}

static
VOID
OFwVideoClearScreen8bpp(
    UCHAR Attr)
{
    PUCHAR p8, LineStart;
    PULONG p32;
    ULONG X, Y;
    UCHAR Color = OFwpVideoAttrToSingleColor(ATT_BG_COLOR(Attr));
    const ULONG PackedData = (Color << 24) | (Color << 16) | (Color << 8) | Color;

    LineStart = VidpBaseAddress;

    Y = VidpScreenHeight;
    while (Y--)
    {
        p32 = (PULONG)LineStart;

        /* Write 4 pixels at a time */
        X = VidpScreenWidth;
        while (X >= 4)
        {
            *p32++ = PackedData;

            X -= 4;
        }

        /* Write the rest of the pixels */
        p8 = (PUCHAR)p32;
        while (X--)
        {
            *p8++ = Color;
        }

        LineStart += VidpScreenStride;
    }
}

static
VOID
OFwVideoClearScreen16bpp(
    UCHAR Attr)
{
    PUCHAR LineStart;
    PULONG p32;
    PUSHORT p16;
    ULONG X, Y;
    USHORT Color = OFwpVideoAttrToSingleColor(ATT_BG_COLOR(Attr));
    const ULONG PackedData = (Color << 16) | Color;

    LineStart = VidpBaseAddress;

    Y = VidpScreenHeight;
    while (Y--)
    {
        p32 = (PULONG)LineStart;

        /* Write 2 pixels at a time */
        X = VidpScreenWidth;
        while (X >= 2)
        {
            *p32++ = PackedData;

            X -= 2;
        }

        /* Write the rest of the pixels */
        p16 = (PUSHORT)p32;
        while (X--)
        {
            *p16++ = Color;
        }

        LineStart += VidpScreenStride;
    }
}

static
VOID
OFwVideoClearScreen888(
    UCHAR Attr)
{
    ULONG X, Y;
    PULONG p32;
    PUCHAR LineStart, p8;
    ULONG Color = OFwpVideoAttrToSingleColor(ATT_BG_COLOR(Attr));
    const ULONG PackedData1 = (Color << 24) | Color;
    const ULONG PackedData2 = ((PackedData1 & 0xFF00) << 16) | (PackedData1 >> 8);
    const ULONG PackedData3 = ((PackedData2 & 0xFF00) << 16) | (PackedData2 >> 8);

    LineStart = VidpBaseAddress;

    Y = VidpScreenHeight;
    while (Y--)
    {
        p32 = (PULONG)LineStart;

        /* Write 4 pixels at a time */
        X = VidpScreenWidth;
        while (X >= 4)
        {
            *p32++ = PackedData1;
            *p32++ = PackedData2;
            *p32++ = PackedData3;

            X -= 4;
        }

        /* Write the rest of the pixels */
        p8 = (PUCHAR)p32;
        while (X--)
        {
            *p8++ = (UCHAR)Color;
            *p8++ = (UCHAR)(Color >> 8);
            *p8++ = (UCHAR)(Color >> 16);
        }

        LineStart += VidpScreenStride;
    }
}

static
VOID
OFwVideoClearScreen8888(
    UCHAR Attr)
{
    PULONG Pixel;
    PUCHAR LineStart;
    ULONG X, Y;
    ULONG Color = OFwpVideoAttrToSingleColor(ATT_BG_COLOR(Attr));

    LineStart = VidpBaseAddress;

    Y = VidpScreenHeight;
    while (Y--)
    {
        Pixel = (PULONG)LineStart;

        X = VidpScreenWidth;
        while (X--)
        {
            *Pixel++ = Color;
        }

        LineStart += VidpScreenStride;
    }
}

static
PUCHAR
OFwpVideoGetFontPtr(
    _In_ ULONG Ch)
{
#if defined(SARCH_OLPC)
    /* Use the font returned from firmware */
    return VidpFontBase + VidpFontCharSize * Ch;
#else
    /* Use the built-in bitmap font */
    return BitmapFont8x16 + Ch * CHAR_HEIGHT;
#endif
}

static
VOID
OFwpVideoDisplayCharacter(
    _In_ ULONG_PTR LineStart,
    _In_ ULONG Ch,
    _In_ ULONG FgColor,
    _In_ ULONG BgColor)
{
    ULONG Height = CHAR_HEIGHT;
    ULONG NextPixelOffset = RTL_BITS_OF(ULONG) - VidpBytesPerPixel * 8;
    PUCHAR FontBitmap = OFwpVideoGetFontPtr(Ch);

    while (Height--)
    {
        ULONG_PTR Pixel = LineStart;
        ULONG Shift = 0;
        ULONG PackedData = 0;
        ULONG Width = CHAR_WIDTH;
        ULONG PixelMask = 1 << (RTL_BITS_OF(UCHAR) - 1);
        ULONG Offset;
        PUCHAR FontChar = FontBitmap;

        /* Check if the first pixel is not aligned on a longword boundary */
        Offset = Pixel & (sizeof(ULONG) - 1);
        if (Offset)
        {
            switch (VidpBytesPerPixel)
            {
                case 1: /* 8-bpp */
                {
                    ULONG BytesNeeded = sizeof(ULONG) - Offset;

                    while (BytesNeeded && Width)
                    {
                        ULONG Color = (*FontChar & PixelMask) ? FgColor : BgColor;

                        *(PUCHAR)Pixel = (UCHAR)Color;

                        /* Start the next pixel */
                        Pixel += 1;
                        PixelMask >>= 1;

                        --BytesNeeded;
                        --Width;
                    }

                    break;
                }
                case 2: /* 565, 555 */
                {
                    ULONG Color = (*FontChar & PixelMask) ? FgColor : BgColor;

                    *(PUSHORT)Pixel = (USHORT)Color;

                    /* Start the next pixel */
                    Pixel += sizeof(USHORT);
                    PixelMask >>= 1;

                    --Width;
                    break;
                }
                case 3: /* 888 */
                {
                    ULONG Color = (*FontChar & PixelMask) ? FgColor : BgColor;

                    switch (Offset)
                    {
                        case 1: /* |*O|OO| */
                        {
                            *(PUCHAR)Pixel = (UCHAR)Color;
                            *(PUSHORT)(Pixel + 1) = (USHORT)(Color >> 8);
                            break;
                        }
                        case 2: /* |**|OO| */
                        {
                            *(PUSHORT)Pixel = (USHORT)Color;

                            Shift = 8;
                            break;
                        }
                        case 3: /* |**|*O| */
                        {
                            *(PUCHAR)Pixel = (UCHAR)Color;

                            Shift = 16;
                            break;
                        }

                        DEFAULT_UNREACHABLE;
                    }

                    PackedData = Color >> (RTL_BITS_OF(ULONG) - (Offset * 8));

                    /* Start the next pixel */
                    Pixel += sizeof(ULONG) - Offset;
                    PixelMask >>= 1;

                    --Width;
                    break;
                }

                DEFAULT_UNREACHABLE;
            }
        }

        /* Write 32-bit chunks */
        while (Width--)
        {
            ULONG Color = (*FontChar & PixelMask) ? FgColor : BgColor;

            PackedData |= Color << Shift;

            if (Shift >= NextPixelOffset)
            {
                *(PULONG)Pixel = PackedData;

                Pixel += sizeof(ULONG);

                if (Shift == NextPixelOffset)
                {
                    PackedData = 0;
                }
                else
                {
                    PackedData = Color >> (RTL_BITS_OF(ULONG) - Shift);
                }
            }

            PixelMask >>= 1;
            if (!PixelMask)
            {
                PixelMask = 1 << (RTL_BITS_OF(UCHAR) - 1);

                ++FontChar;
            }

            Shift = (Shift + (VidpBytesPerPixel * 8)) % RTL_BITS_OF(ULONG);
        }

        /* Write the rest of the pixels */
        switch (Shift)
        {
            case 0:
                break;

            case 8:  /* |O*|**| */
                *(PUCHAR)Pixel = (UCHAR)PackedData;
                break;

            case 16: /* |OO|**| */
                *(PUSHORT)Pixel = (USHORT)PackedData;
                break;

            case 24: /* |OO|O*| */
                *(PUSHORT)Pixel = (USHORT)PackedData;
                *(PUCHAR)(Pixel + 2) = (UCHAR)(PackedData >> 16);
                break;

            DEFAULT_UNREACHABLE;
        }

        FontBitmap += CHAR_STRIDE;
        LineStart += VidpScreenStride;
    }
}

static
VIDEODISPLAYMODE
OFwVideoSetDisplayMode(
    char* DisplayModeName,
    BOOLEAN Init)
{
    /* There seems to be no way to change the display mode via OFW */
    return VideoTextMode;
}

static
VOID
OFwVideoGetDisplaySize(
    PULONG Width,
    PULONG Height,
    PULONG Depth)
{
    *Width = VidpScreenWidth / CHAR_WIDTH;
    *Height = VidpScreenHeight / CHAR_HEIGHT;
    *Depth = VidpDepth;
}

static
ULONG
OFwVideoGetBufferSize(VOID)
{
    ULONG Width, Height, Depth;

    OFwVideoGetDisplaySize(&Width, &Height, &Depth);

    return (Width * Height) * FREELDR_CHAR_SIZE;
}

static
VOID
OFwVideoGetFontsFromFirmware(
    PULONG RomFontPointers)
{
    /* Not supported */
    RomFontPointers[0] = 0;
    RomFontPointers[1] = 0;
    RomFontPointers[2] = 0;
    RomFontPointers[3] = 0;
    RomFontPointers[4] = 0;
    RomFontPointers[5] = 0;
}

static
VOID
OFwVideoSetTextCursorPosition(
    UCHAR X,
    UCHAR Y)
{
    /* TODO: Should be emulated in the UI code */
}

static
VOID
OFwVideoHideShowTextCursor(
    BOOLEAN Show)
{
    /* TODO: Should be emulated in the UI code */
}

static
VOID
OFwVideoVideoPutChar(
    int Ch,
    UCHAR Attr,
    unsigned X,
    unsigned Y)
{
    ULONG_PTR Pixel;
    ULONG FgColor, BgColor;

    FgColor = OFwpVideoAttrToSingleColor(ATT_FG_COLOR(Attr));
    BgColor = OFwpVideoAttrToSingleColor(ATT_BG_COLOR(Attr));

    Pixel = (ULONG_PTR)VidpBaseAddress +
            Y * VidpScreenStride * CHAR_HEIGHT +
            X * VidpBytesPerPixel * CHAR_WIDTH;

    OFwpVideoDisplayCharacter(Pixel, Ch, FgColor, BgColor);
}

static
VOID
OFwVideoCopyOffScreenBufferToVRAM(
    PVOID Buffer)
{
    ULONG X, Y;
    ULONG_PTR LineStart, Pixel;
    PUCHAR OffScreenBuffer = Buffer;

    LineStart = (ULONG_PTR)VidpBaseAddress;

    Y = VidpScreenHeight / CHAR_HEIGHT;
    while (Y--)
    {
        Pixel = LineStart;

        X = VidpScreenWidth / CHAR_WIDTH;
        while (X--)
        {
            UCHAR Char = OffScreenBuffer[0], Attr = OffScreenBuffer[1];
            ULONG FgColor = OFwpVideoAttrToSingleColor(ATT_FG_COLOR(Attr));
            ULONG BgColor = OFwpVideoAttrToSingleColor(ATT_BG_COLOR(Attr));

            OFwpVideoDisplayCharacter(Pixel, Char, FgColor, BgColor);

            OffScreenBuffer += FREELDR_CHAR_SIZE;
            Pixel += VidpBytesPerPixel * CHAR_WIDTH;
        }

        LineStart += VidpScreenStride * CHAR_HEIGHT;
    }
}

static
BOOLEAN
OFwVideoIsPaletteFixed(VOID)
{
    return VidpIsPaletteFixed;
}

static
VOID
OFwVideoSetPaletteColor(
    UCHAR Color,
    UCHAR Red,
    UCHAR Green,
    UCHAR Blue)
{
    /* TODO: call-method set-colors */
}

static
VOID
OFwVideoGetPaletteColor(
    UCHAR Color,
    UCHAR* Red,
    UCHAR* Green,
    UCHAR* Blue)
{
    /* TODO: call-method get-colors */
}

static
VOID
OFwVideoSync(VOID)
{
    NOTHING;
}

#if defined(SARCH_OLPC)
static
BOOLEAN
OFwpInitializeFontParameters(VOID)
{
    PPSF2_HEADER PsfFont;

    if (!OFwInterpret("romfont", (PVOID)&PsfFont))
        return FALSE;

    if (PsfFont->Magic != 0x864AB572)
        return FALSE;

    VidpFontBase = (PUCHAR)PsfFont + PsfFont->HeaderSize;
    VidpFontCharSize = PsfFont->CharSize;
    VidpFontCharHeight = PsfFont->Height;
    VidpFontCharWidth = PsfFont->Width;
    VidpFontCharStride = (VidpFontCharWidth + 7) / 8;

    return TRUE;
}
#endif

BOOLEAN
OFwVideoInit(VOID)
{
    OFW_PHANDLE DeviceNode;
    CHAR Buffer[128];

    if (!OFwInstanceToPackage(OFwpStdOutHandle, &DeviceNode))
        return FALSE;

    if (!OFwGetProperty(DeviceNode, "device_type", Buffer, sizeof(Buffer), NULL))
        return FALSE;

    if (strncmp(Buffer, "display", 7) != 0)
        return FALSE;

    if (!OFwGetPropertyUlong(DeviceNode, "width", &VidpScreenWidth))
        return FALSE;

    if (!OFwGetPropertyUlong(DeviceNode, "height", &VidpScreenHeight))
        return FALSE;

    if (!OFwGetPropertyUlong(DeviceNode, "depth", &VidpDepth))
        return FALSE;

    if (!OFwGetPropertyUlong(DeviceNode, "linebytes", &VidpScreenStride))
        return FALSE;

    if (!OFwGetPropertyUlong(DeviceNode, "address", (PULONG)&VidpBaseAddress))
        return FALSE;

#if defined(SARCH_OLPC)
    if (!OFwpInitializeFontParameters())
        return FALSE;
#endif

    VidpBytesPerPixel = (VidpDepth + 7) / 8;

    switch (VidpDepth)
    {
        case 8:
            VidpColorMode = 0;
            MachVtbl.VideoClearScreen = OFwVideoClearScreen8bpp;
            break;
        case 15:
            VidpColorMode = 1;
            MachVtbl.VideoClearScreen = OFwVideoClearScreen16bpp;
            break;
        case 16:
            VidpColorMode = 2;
            MachVtbl.VideoClearScreen = OFwVideoClearScreen16bpp;
            break;
        case 24:
            VidpColorMode = 3;
            MachVtbl.VideoClearScreen = OFwVideoClearScreen888;
            break;
        case 32:
            VidpColorMode = 4;
            MachVtbl.VideoClearScreen = OFwVideoClearScreen8888;
            break;

        default:
            return FALSE;
    }

    /* TODO: Set this to false when both the get- and set- methods are present */
    VidpIsPaletteFixed = TRUE;

    MachVtbl.VideoSetDisplayMode            = OFwVideoSetDisplayMode;
    MachVtbl.VideoGetDisplaySize            = OFwVideoGetDisplaySize;
    MachVtbl.VideoGetBufferSize             = OFwVideoGetBufferSize;
    MachVtbl.VideoGetFontsFromFirmware      = OFwVideoGetFontsFromFirmware;
    MachVtbl.VideoSetTextCursorPosition     = OFwVideoSetTextCursorPosition;
    MachVtbl.VideoHideShowTextCursor        = OFwVideoHideShowTextCursor;
    MachVtbl.VideoPutChar                   = OFwVideoVideoPutChar;
    MachVtbl.VideoCopyOffScreenBufferToVRAM = OFwVideoCopyOffScreenBufferToVRAM;
    MachVtbl.VideoIsPaletteFixed            = OFwVideoIsPaletteFixed;
    MachVtbl.VideoSetPaletteColor           = OFwVideoSetPaletteColor;
    MachVtbl.VideoGetPaletteColor           = OFwVideoGetPaletteColor;
    MachVtbl.VideoSync                      = OFwVideoSync;

    return TRUE;
}

#define KM_FB              0xFFC00000
#define KM_FB_BPP          2
#define KM_WIDTH           1200
#define KM_HEIGHT          (900 / 2)
#define TOP_BOTTOM_LINES   0

VOID
KmVideoScrollUp(VOID)
{
    ULONG PixelCount, BgColor;
    PUSHORT Src = (PUSHORT)((PUCHAR)KM_FB + CHAR_HEIGHT * VidpScreenStride);
    PUSHORT Dst = (PUSHORT)((PUCHAR)KM_FB);

    PixelCount = VidpScreenStride * (KM_HEIGHT - CHAR_HEIGHT) / KM_FB_BPP;

    while (PixelCount--)
        *Dst++ = *Src++;

    BgColor = OFwpVideoAttrToSingleColor(ATT_BG_COLOR(COLOR_BLACK));

    for (PixelCount = 0; PixelCount < ((VidpScreenStride * CHAR_HEIGHT) / KM_FB_BPP); PixelCount++)
        *Dst++ = BgColor;
}

VOID
OFwConsPutCharKernelMode(
    int c)
{
    BOOLEAN NeedScroll;
    static ULONG CurrentCursorX = 0;
    static ULONG CurrentCursorY = 0;

    NeedScroll = (CurrentCursorY >= (KM_HEIGHT/CHAR_HEIGHT));
    if (NeedScroll)
    {
        KmVideoScrollUp();
        --CurrentCursorY;
    }

    if (c == '\r')
    {
        CurrentCursorX = 0;
    }
    else if (c == '\n')
    {
        CurrentCursorX = 0;

        if (!NeedScroll)
            ++CurrentCursorY;
    }
    else if (c == '\t')
    {
        CurrentCursorX = (CurrentCursorX + 8) & ~ 7;
    }
    else
    {
        ULONG FgColor = OFwpVideoAttrToSingleColor(ATT_FG_COLOR(0x0f));
        ULONG BgColor = OFwpVideoAttrToSingleColor(ATT_BG_COLOR(0x0f));

        ULONG_PTR Pixel = (ULONG_PTR)KM_FB +
                          CurrentCursorY * VidpScreenStride * CHAR_HEIGHT +
                          CurrentCursorX * VidpBytesPerPixel * CHAR_WIDTH;

        OFwpVideoDisplayCharacter(Pixel, c, FgColor, BgColor);
        CurrentCursorX++;
    }

    if (CurrentCursorX >= (KM_WIDTH / CHAR_WIDTH))
    {
        CurrentCursorX = 0;
        CurrentCursorY++;
    }
}
