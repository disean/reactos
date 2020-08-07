/*
 * PROJECT:     Hardware tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main file
 * COPYRIGHT:   Copyright 2020 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>
#include <drivers/pc98/video.h>
#include <drivers/pc98/cpu.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

extern unsigned int delay_count;
extern UCHAR BitmapFont8x16[256 * 16];

/* GLOBALS ********************************************************************/

#define START_TEST(name) VOID Test_##name(VOID)

typedef VOID TESTFUNC(VOID);
typedef TESTFUNC *PTESTFUNC;

typedef struct
{
    const char *TestName;
    TESTFUNC *TestFunction;
} TEST, *PTEST;

TESTFUNC Test_DumpMemory;
TESTFUNC Test_DumpMemory2;
TESTFUNC Test_DumpIo;
TESTFUNC Test_Ide;
TESTFUNC Test_Graph;
TESTFUNC Test_Graph_Text;
TESTFUNC Test_Graph_GdcGrcg;
TESTFUNC Test_Graph_GdcGrcgEgc;

TEST TestList[] =
{
    { "DumpMemory", Test_DumpMemory },
    { "DumpMemory2", Test_DumpMemory2 },
    { "DumpIo", Test_DumpIo },
    { "Ide", Test_Ide },
    { "Graph", Test_Graph },
    { "Graph_Text", Test_Graph_Text },
    { "Graph_GdcGrcg", Test_Graph_GdcGrcg },
    { "Graph_GdcGrcgEgc", Test_Graph_GdcGrcgEgc },
    { NULL, NULL }
};

static UCHAR BoxPattern[16] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static const UCHAR BitReverseLookupTable[256] =
{
    0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0, /* 0 */
    0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8, /* 1 */
    0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4, /* 2 */
    0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC, /* 3 */
    0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2, /* 4 */
    0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA, /* 5 */
    0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6, /* 6 */
    0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE, /* 7 */
    0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1, /* 8 */
    0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9, /* 9 */
    0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5, /* A */
    0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD, /* B */
    0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3, /* C */
    0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB, /* D */
    0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7, /* E */
    0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF  /* F */
};

/* FUNCTIONS ******************************************************************/

static VOID
PrintNextMessage(
    _In_ ULONG TestNumber,
    _In_ PCSTR TestName)
{
    Pc98ConsSetCursorPosition(63, 19);
    ERR("%s\n", TestName);
    Pc98ConsSetCursorPosition(63, 20);
    ERR("Done [%d/%d]\n", TestNumber, RTL_NUMBER_OF(TestList) - 1);
    Pc98ConsSetCursorPosition(63, 22);
    ERR("Press any key\n");
}

static VOID
SelectIdeDevice(
    _In_ UCHAR Channel,
    _In_ UCHAR DeviceNumber)
{
    WRITE_PORT_UCHAR((PUCHAR)0x432, Channel);
    StallExecutionProcessor(5);

    WRITE_PORT_UCHAR((PUCHAR)0x64C, DeviceNumber ? 0xB0 : 0xA0);
}

static VOID
DumpIdeRegs(
    _In_ UCHAR Left)
{
    static ULONG IdeIoList[] =
    { 0x430, 0x432, 0x435, 0x640, 0x642, 0x644, 0x646, 0x648, 0x64A, 0x64C, 0x64E };
    ULONG IoEntry;
    UCHAR Data;

    for (IoEntry = 0;
         IoEntry < RTL_NUMBER_OF(IdeIoList);
         ++IoEntry)
    {
        Data = READ_PORT_UCHAR(UlongToPtr(IdeIoList[IoEntry]));

        Pc98ConsSetCursorPosition(Left * 16, (IoEntry % 22) + 4);
        ERR("0x%03X 0x%02X\n", IdeIoList[IoEntry], Data);
        StallExecutionProcessor(20);
    }
}

static inline
VOID
SetExecutionAddress(
    _In_ USHORT X,
    _In_ USHORT Y,
    _In_ UCHAR Plane)
{
    CSRWPARAM CursorPosition;
    ULONG ExecutionAddress = (VRAM_PLANE_SIZE / 2) * (Plane + 1);

    CursorPosition.CursorAddress = ExecutionAddress + (X >> 4) + Y * (80 / 2);
    CursorPosition.DotAddress = X & 0x0F;
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_COMMAND, GDC_COMMAND_CSRW);
    WRITE_GDC_CSRW((PUCHAR)GDC2_IO_o_PARAM, &CursorPosition);
}

static VOID
GdcSolidColorFill(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ UCHAR Color)
{
    UCHAR Pattern, Plane, ColorMask;

    Right = (Right - Left) + 1;
    Bottom = (Bottom - Top) + 1;

    WRITE_GDC2_COMMAND(GDC_COMMAND_TEXTW);
    for (Pattern = 0; Pattern < 8; Pattern++)
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0xFF);

    for (Plane = 0, ColorMask = 1;
         Plane < 4;
         ++Plane, ColorMask <<= 1)
    {
        if (Color & ColorMask)
            WRITE_GDC2_COMMAND(GDC_COMMAND_WRITE | GDC_MOD_REPLACE);
        else
            WRITE_GDC2_COMMAND(GDC_COMMAND_WRITE | GDC_MOD_CLEAR);

        SetExecutionAddress(Left, Top, Plane);

        WRITE_GDC2_COMMAND(GDC_COMMAND_FIGS);
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x10);
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, FIRSTBYTE(Right));
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, SECONDBYTE(Right) | GDC_GRAPHICS_DRAWING);
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, FIRSTBYTE(Bottom));
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, SECONDBYTE(Bottom));

        WRITE_GDC2_COMMAND(GDC_COMMAND_GCHRD);
    }
}

static inline
VOID
GdcSetPixel(
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ UCHAR Color)
{
    UCHAR Plane, ColorMask;

    for (Plane = 0, ColorMask = 1;
         Plane < 4;
         Plane++, ColorMask <<= 1)
    {
        SetExecutionAddress(X, Y, Plane);

        WRITE_GDC2_COMMAND(GDC_COMMAND_FIGS);
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x00);

        if (Color & ColorMask)
            WRITE_GDC2_COMMAND(GDC_COMMAND_WRITE | GDC_MOD_REPLACE);
        else
            WRITE_GDC2_COMMAND(GDC_COMMAND_WRITE | GDC_MOD_CLEAR);

        WRITE_GDC2_COMMAND(GDC_COMMAND_FIGD);
    }
}

static inline
VOID
GrcgOn(VOID)
{
    WRITE_PORT_UCHAR((PUCHAR)GRCG_IO_o_MODE, GRCG_MODE_READ_MODIFY_WRITE);
}

static inline
VOID
GrcgColor(
    _In_ UCHAR Color)
{
    WRITE_PORT_UCHAR((PUCHAR)GRCG_IO_o_MODE, GRCG_MODE_READ_MODIFY_WRITE);
    WRITE_PORT_UCHAR((PUCHAR)GRCG_IO_o_TILE_PATTERN, (Color & 1) ? 0xFF : 0);
    WRITE_PORT_UCHAR((PUCHAR)GRCG_IO_o_TILE_PATTERN, (Color & 2) ? 0xFF : 0);
    WRITE_PORT_UCHAR((PUCHAR)GRCG_IO_o_TILE_PATTERN, (Color & 4) ? 0xFF : 0);
    WRITE_PORT_UCHAR((PUCHAR)GRCG_IO_o_TILE_PATTERN, (Color & 8) ? 0xFF : 0);
}

static inline
VOID
GrcgOff(VOID)
{
    WRITE_PORT_UCHAR((PUCHAR)GRCG_IO_o_MODE, 0);
}

static VOID
GrcgSolidColorFill(
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG Right,
    _In_ ULONG Bottom,
    _In_ UCHAR Color)
{
    ULONG X;

    GrcgColor(Color);
    for (; Top <= Bottom; Top++)
    {
        for (X = Left; X <= Right; X++)
        {
            *(PUCHAR)(VRAM_NORMAL_PLANE_B + (X / 8) + Top * 80) = 0x80 >> (X % 8);
        }
    }
}

static VOID
GdcDisplayCharacterEx(
    _In_ PUCHAR FontPtr,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Color)
{
    UCHAR Part, Line;
    UCHAR Plane, ColorMask;

    for (Part = 0; Part < 2; Part++)
    {
        WRITE_GDC2_COMMAND(GDC_COMMAND_TEXTW);
        for (Line = 0; Line < 8; Line++)
            WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, BitReverseLookupTable[*FontPtr++]);

        for (Plane = 0, ColorMask = 1;
             Plane < 4;
             ++Plane, ColorMask <<= 1)
        {
            if (Color & ColorMask)
                WRITE_GDC2_COMMAND(GDC_COMMAND_WRITE | GDC_MOD_REPLACE);
            else
                WRITE_GDC2_COMMAND(GDC_COMMAND_WRITE | GDC_MOD_CLEAR);

            SetExecutionAddress(X, Y + Part * 8, Plane);

            WRITE_GDC2_COMMAND(GDC_COMMAND_FIGS);
            WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x12);
            WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x07);
            WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x00);

            WRITE_GDC2_COMMAND(GDC_COMMAND_GCHRD);
        }
    }
}

static VOID
GdcDisplayCharacter(
    _In_ UCHAR Character,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG TextColor,
    _In_ ULONG BackColor)
{
    if (BackColor != 13)
        GdcDisplayCharacterEx(BoxPattern, Left, Top, BackColor);

    GdcDisplayCharacterEx(BitmapFont8x16 + Character * 16, Left, Top, 12);
}

static VOID
GrcgDisplayCharacterEx(
    _In_ PUCHAR FontPtr,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Color)
{
    ULONG i;

    GrcgColor(Color);
    for (i = 0; i < 16; i++)
    {
        *(PUCHAR)(VRAM_NORMAL_PLANE_B + (X / 8) + (Y + i) * 80) = *FontPtr++;
    }
}

static VOID
GrcgDisplayCharacter(
    _In_ UCHAR Character,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG TextColor,
    _In_ ULONG BackColor)
{
    if (BackColor != 13)
        GrcgDisplayCharacterEx(BoxPattern, Left, Top, BackColor);

    GrcgDisplayCharacterEx(BitmapFont8x16 + Character * 16, Left, Top, 12);
}

static VOID
GdcGrcgDisplayCharacterEx(
    _In_ PUCHAR FontPtr,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Color)
{
    UCHAR Part, Line;

    GrcgColor(Color);
    for (Part = 0; Part < 2; Part++)
    {
        WRITE_GDC2_COMMAND(GDC_COMMAND_TEXTW);
        for (Line = 0; Line < 8; Line++)
            WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, BitReverseLookupTable[*FontPtr++]);

        WRITE_GDC2_COMMAND(GDC_COMMAND_WRITE | GDC_MOD_SET);

        SetExecutionAddress(X, Y + Part * 8, 0);

        WRITE_GDC2_COMMAND(GDC_COMMAND_FIGS);
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x12);
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x07);
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x00);

        WRITE_GDC2_COMMAND(GDC_COMMAND_GCHRD);
    }
}

static VOID
GdcGrcgDisplayCharacter(
    _In_ UCHAR Character,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG TextColor,
    _In_ ULONG BackColor)
{
    if (BackColor != 13)
        GdcGrcgDisplayCharacterEx(BoxPattern, Left, Top, BackColor);

    GdcGrcgDisplayCharacterEx(BitmapFont8x16 + Character * 16, Left, Top, 12);
}

static VOID
GdcEgcDisplayCharacterEx(
    _In_ PUCHAR FontPtr,
    _In_ ULONG X,
    _In_ ULONG Y,
    _In_ ULONG Color)
{
    UCHAR Part, Line;

    WRITE_PORT_USHORT((PUSHORT)EGC_IO_o_FG_COLOR, Color);
    for (Part = 0; Part < 2; Part++)
    {
        WRITE_GDC2_COMMAND(GDC_COMMAND_TEXTW);
        for (Line = 0; Line < 8; Line++)
            WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, BitReverseLookupTable[*FontPtr++]);

        WRITE_GDC2_COMMAND(GDC_COMMAND_WRITE | GDC_MOD_SET);

        SetExecutionAddress(X, Y + Part * 8, 0);

        WRITE_GDC2_COMMAND(GDC_COMMAND_FIGS);
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x12);
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x07);
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x00);

        WRITE_GDC2_COMMAND(GDC_COMMAND_GCHRD);
    }
}

static VOID
GdcEgcDisplayCharacter(
    _In_ UCHAR Character,
    _In_ ULONG Left,
    _In_ ULONG Top,
    _In_ ULONG TextColor,
    _In_ ULONG BackColor)
{
    if (BackColor != 13)
        GdcEgcDisplayCharacterEx(BoxPattern, Left, Top, BackColor);

    GdcEgcDisplayCharacterEx(BitmapFont8x16 + Character * 16, Left, Top, 12);
}

/* TESTS **********************************************************************/

START_TEST(DumpMemory)
{
    DbgDumpBuffer(DPRINT_MEMORY, (PUCHAR)0x400, 0x180);
}

START_TEST(DumpMemory2)
{
    DbgDumpBuffer(DPRINT_MEMORY, (PUCHAR)(0x400 + 0x180), 0xC0);
    DbgDumpBuffer(DPRINT_MEMORY, (PUCHAR)MEM_EXTENDED_NORMAL, 0x50);
    DbgDumpBuffer(DPRINT_MEMORY, (PUCHAR)0xA3FE2, 0x1C);
}

START_TEST(DumpIo)
{
    static ULONG IoList[] =
    { 0x5C, 0x5D, 0x5E, 0x5F,
      0x30, 0x32, 0x34, 0x130, 0x132, 0x134, 0x136, 0x136, 0x138, 0x13A, 0x434,
      0x238, 0x239, 0x23A, 0x23B, 0x23C, 0x23D, 0x23E, 0x23F,
      0x40, 0x42, 0x44, 0x141, 0x142, 0x149, 0x14B, 0x14D, 0x14E,
      0x41, 0x43,
      0x128, 0x22,
      0x413,
      0x430, 0x432, 0x435,
      0x9A0, 0x9A2, 0x9A8, 0xFAC,
      0x70, 0x72, 0x74, 0x76, 0x78, 0x7A };
    ULONG IoEntry;
    UCHAR Data;

    for (IoEntry = 0;
         IoEntry < RTL_NUMBER_OF(IoList);
         ++IoEntry)
    {
        Data = READ_PORT_UCHAR(UlongToPtr(IoList[IoEntry]));

        Pc98ConsSetCursorPosition((IoEntry / 22) * 16, IoEntry % 22);
        ERR("0x%03X 0x%02X\n", IoList[IoEntry], Data);
        StallExecutionProcessor(20);
    }
}

START_TEST(Ide)
{
    UCHAR Channel, DeviceNumber, Device = 0;

    for (Channel = 0; Channel < 2; Channel++)
    {
        for (DeviceNumber = 0; DeviceNumber < 2; DeviceNumber++)
        {
            SelectIdeDevice(Channel, DeviceNumber);
            StallExecutionProcessor(5);
            DumpIdeRegs(Device);
            WRITE_PORT_UCHAR((PUCHAR)0x646, 0x55);
            WRITE_PORT_UCHAR((PUCHAR)0x646, 0x55);
            StallExecutionProcessor(5);
            Pc98ConsSetCursorPosition(0, Device);
            ERR("Channel %d Device %d 0x%02X\n",
                Channel, DeviceNumber, READ_PORT_UCHAR((PUCHAR)0x646));
            Device++;
        }
    }
}

START_TEST(Graph)
{
    ULONG Color;
    ULONG Ty;
    ULONG Ticks = GetArticTicks();
    USHORT Start, End;
    USHORT Start2, End2;
    ULONG X, Y;

    ERR("T %d = %d, %d, Factor: %d ",
        Ticks, Ticks * 3260, (Ticks * 3260) / 1000, delay_count);

    Start2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Start = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    for (Color = 0, Ty = 0;
         Color < 16;
         ++Color, Ty += 20)
    {
        GdcSolidColorFill(0, Ty, 200, Ty + 20, Color);
    }
    End = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    End2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    ERR("Status 0x%02X ", READ_PORT_UCHAR((PUCHAR)GDC2_IO_i_STATUS));
    StallExecutionProcessor(60000);
    ERR("0x%02X\n", READ_PORT_UCHAR((PUCHAR)GDC2_IO_i_STATUS));
    Pc98ConsSetCursorPosition(0, 21);
    ERR("S %d E %d D %d\n", Start, End, abs(End - Start));
    Pc98ConsSetCursorPosition(0, 22);
    ERR("S2 %d E2 %d D2 %d\n", Start2, End2, abs(End2 - Start2));

    Start2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Start = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    GrcgOn();
    for (Color = 0, Ty = 0;
         Color < 16;
         ++Color, Ty += 20)
    {
        GrcgSolidColorFill(200, Ty, 400, Ty + 20, Color);
    }
    GrcgOff();
    End = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    End2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Pc98ConsSetCursorPosition(25, 21);
    ERR("S %d E %d D %d\n", Start, End, abs(End - Start));
    Pc98ConsSetCursorPosition(25, 22);
    ERR("S2 %d E2 %d D2 %d\n", Start2, End2, abs(End2 - Start2));

    Start2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Start = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    for (Y = 20; Y < 200; Y++)
    {
        for (X = 0; X < 200; X++)
        {
            GdcSetPixel(X, Y, 12);
        }
    }
    End = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    End2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Pc98ConsSetCursorPosition(52, 14);
    ERR("S %d E %d D %d\n", Start, End, abs(End - Start));
    Pc98ConsSetCursorPosition(52, 15);
    ERR("S2 %d E2 %d D2 %d\n", Start2, End2, abs(End2 - Start2));
}

START_TEST(Graph_Text)
{
    ULONG i, X, Y;
    USHORT Start, End;
    USHORT Start2, End2;
    PCHAR TestString = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
                       "incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis"
                       "nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat."
                       "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu"
                       "fugiat nulla pariatur";

    Start2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Start = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    for (i = 0, X = 2, Y = 8 * 2;
         i < strlen(TestString);
         i++, X++)
    {
        if (X % 70 == 0)
        {
            X = 2;
            Y += 16;
        }
        GdcDisplayCharacter(TestString[i], X * 8, Y, X % 15, i % 15);
    }
    End = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    End2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Pc98ConsSetCursorPosition(28, 16);
    ERR("S %d E %d D %d\n", Start, End, abs(End - Start));
    Pc98ConsSetCursorPosition(28, 17);
    ERR("S2 %d E2 %d D2 %d\n", Start2, End2, abs(End2 - Start2));
    StallExecutionProcessor(60000);

    Start2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Start = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    GrcgOn();
    for (i = 0, X = 2, Y = 8 * 12;
         i < strlen(TestString);
         i++, X++)
    {
        if (X % 70 == 0)
        {
            X = 2;
            Y += 16;
        }
        GrcgDisplayCharacter(TestString[i], X * 8, Y, X % 15, i % 15);
    }
    GrcgOff();
    End = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    End2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Pc98ConsSetCursorPosition(28, 18);
    ERR("S %d E %d D %d\n", Start, End, abs(End - Start));
    Pc98ConsSetCursorPosition(28, 19);
    ERR("S2 %d E2 %d D2 %d\n", Start2, End2, abs(End2 - Start2));
}

START_TEST(Graph_GdcGrcg)
{
    UCHAR Pattern;
    USHORT Start, End;
    USHORT Start2, End2;

    Start2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Start = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    GrcgOn();
    GrcgColor(12);
    WRITE_GDC2_COMMAND(GDC_COMMAND_TEXTW);
    for (Pattern = 0; Pattern < 8; Pattern++)
        WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0xFF);

    WRITE_GDC2_COMMAND(GDC_COMMAND_WRITE | GDC_MOD_SET);

    SetExecutionAddress(16, 16, 0);

    WRITE_GDC2_COMMAND(GDC_COMMAND_FIGS);
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, 0x10);
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, FIRSTBYTE(543));
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, SECONDBYTE(543) | GDC_GRAPHICS_DRAWING);
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, FIRSTBYTE(120));
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_PARAM, SECONDBYTE(120));

    WRITE_GDC2_COMMAND(GDC_COMMAND_GCHRD);
    GrcgOff();
    End = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    End2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Pc98ConsSetCursorPosition(28, 18);
    ERR("S %d E %d D %d\n", Start, End, abs(End - Start));
    Pc98ConsSetCursorPosition(28, 19);
    ERR("S2 %d E2 %d D2 %d\n", Start2, End2, abs(End2 - Start2));
    StallExecutionProcessor(60000);

    Start2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Start = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    GdcSolidColorFill(16, 16 * 10, 542 + 16, 16 * 10 + 119, 12);
    End = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    End2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Pc98ConsSetCursorPosition(28, 20);
    ERR("S %d E %d D %d\n", Start, End, abs(End - Start));
    Pc98ConsSetCursorPosition(28, 21);
    ERR("S2 %d E2 %d D2 %d\n", Start2, End2, abs(End2 - Start2));
}

START_TEST(Graph_GdcGrcgEgc)
{
    ULONG i, X, Y;
    USHORT Start, End;
    USHORT Start2, End2;
    PCHAR TestString = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
                       "incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis"
                       "nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat."
                       "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu"
                       "fugiat nulla pariatur";

    Start2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Start = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    GrcgOn();
    for (i = 0, X = 2, Y = 8 * 2;
         i < strlen(TestString);
         i++, X++)
    {
        if (X % 70 == 0)
        {
            X = 2;
            Y += 16;
        }
        GdcGrcgDisplayCharacter(TestString[i], X * 8, Y, X % 15, i % 15);
    }
    GrcgOff();
    End = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    End2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Pc98ConsSetCursorPosition(28, 16);
    ERR("S %d E %d D %d\n", Start, End, abs(End - Start));
    Pc98ConsSetCursorPosition(28, 17);
    ERR("S2 %d E2 %d D2 %d\n", Start2, End2, abs(End2 - Start2));
    StallExecutionProcessor(60000);

    Start2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Start = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_MODE_FLIPFLOP2, GDC2_EGC_FF_UNPROTECT);
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_MODE_FLIPFLOP2, GDC2_MODE_EGC);
    WRITE_PORT_UCHAR((PUCHAR)GRCG_IO_o_MODE, GRCG_ENABLE);
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_MODE_FLIPFLOP2, GDC2_EGC_FF_PROTECT);
    WRITE_PORT_USHORT((PUSHORT)EGC_IO_o_PLANE_ACCESS, 0xFFF0);
    WRITE_PORT_USHORT((PUSHORT)EGC_IO_o_PATTERN_DATA_PLANE_READ, 0x40FF);
    WRITE_PORT_USHORT((PUSHORT)EGC_IO_o_READ_WRITE_MODE, 0x0CAC);
    WRITE_PORT_USHORT((PUSHORT)EGC_IO_o_MASK, 0xFFFF);
    WRITE_PORT_USHORT((PUSHORT)EGC_IO_o_BIT_ADDRESS, 0);
    WRITE_PORT_USHORT((PUSHORT)EGC_IO_o_BIT_LENGTH, 0x000F);
    for (i = 0, X = 2, Y = 8 * 12;
         i < strlen(TestString);
         i++, X++)
    {
        if (X % 70 == 0)
        {
            X = 2;
            Y += 16;
        }
        GdcEgcDisplayCharacter(TestString[i], X * 8, Y, X % 15, i % 15);
    }
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_MODE_FLIPFLOP2, GDC2_EGC_FF_UNPROTECT);
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_MODE_FLIPFLOP2, GDC2_MODE_GRCG);
    WRITE_PORT_UCHAR((PUCHAR)GRCG_IO_o_MODE, GRCG_DISABLE);
    WRITE_PORT_UCHAR((PUCHAR)GDC2_IO_o_MODE_FLIPFLOP2, GDC2_EGC_FF_PROTECT);
    End = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_0);
    End2 = READ_PORT_USHORT((PUSHORT)CPU_IO_i_ARTIC_2);
    Pc98ConsSetCursorPosition(28, 18);
    ERR("S %d E %d D %d\n", Start, End, abs(End - Start));
    Pc98ConsSetCursorPosition(28, 19);
    ERR("S2 %d E2 %d D2 %d\n", Start2, End2, abs(End2 - Start2));
}

VOID
RunTests(VOID)
{
    PTEST TestEntry;
    ULONG TestNumber;

    DebugEnableScreenPort();

    for (TestEntry = TestList, TestNumber = 1;
         TestEntry->TestName;
         ++TestEntry, ++TestNumber)
    {
        MachVideoClearScreen(ATTR(COLOR_WHITE, COLOR_BLACK));
        Pc98ConsSetCursorPosition(0, 0);
        TestEntry->TestFunction();
        PrintNextMessage(TestNumber, TestEntry->TestName);
        MachConsGetCh();
    }

    DebugDisableScreenPort();
}
