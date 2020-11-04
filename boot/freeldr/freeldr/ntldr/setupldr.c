/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Windows-compatible NT OS Setup Loader.
 * COPYRIGHT:   Copyright 2009-2019 Aleksey Bragin <aleksey@reactos.org>
 */

#include <freeldr.h>
#include <ndk/ldrtypes.h>
#include <arc/setupblk.h>
#include "winldr.h"
#include "inffile.h"
#include "ntldropts.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

#define TAG_BOOT_OPTIONS 'pOtB'

// TODO: Move to .h
VOID
AllocateAndInitLPB(
    IN USHORT VersionToBoot,
    OUT PLOADER_PARAMETER_BLOCK* OutLoaderBlock);

static VOID
SetupLdrLoadNlsData(PLOADER_PARAMETER_BLOCK LoaderBlock, HINF InfHandle, PCSTR SearchPath)
{
    INFCONTEXT InfContext;
    PCSTR AnsiName, OemName, LangName;

    /* Get ANSI codepage file */
    if (!InfFindFirstLine(InfHandle, "NLS", "AnsiCodepage", &InfContext))
    {
        ERR("Failed to find 'NLS/AnsiCodepage'\n");
        return;
    }
    if (!InfGetDataField(&InfContext, 1, &AnsiName))
    {
        ERR("Failed to get load options\n");
        return;
    }

    /* Get OEM codepage file */
    if (!InfFindFirstLine(InfHandle, "NLS", "OemCodepage", &InfContext))
    {
        ERR("Failed to find 'NLS/AnsiCodepage'\n");
        return;
    }
    if (!InfGetDataField(&InfContext, 1, &OemName))
    {
        ERR("Failed to get load options\n");
        return;
    }

    if (!InfFindFirstLine(InfHandle, "NLS", "UnicodeCasetable", &InfContext))
    {
        ERR("Failed to find 'NLS/AnsiCodepage'\n");
        return;
    }
    if (!InfGetDataField(&InfContext, 1, &LangName))
    {
        ERR("Failed to get load options\n");
        return;
    }

    TRACE("NLS data '%s' '%s' '%s'\n", AnsiName, OemName, LangName);

#if DBG
    {
        BOOLEAN Success = WinLdrLoadNLSData(LoaderBlock, SearchPath, AnsiName, OemName, LangName);
        (VOID)Success;
        TRACE("NLS data loading %s\n", Success ? "successful" : "failed");
    }
#else
    WinLdrLoadNLSData(LoaderBlock, SearchPath, AnsiName, OemName, LangName);
#endif

    /* TODO: Load OEM HAL font */
    // Value "OemHalFont"
}

static
BOOLEAN
SetupLdrInitErrataInf(
    IN OUT PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN HINF InfHandle,
    IN PCSTR SystemRoot)
{
    INFCONTEXT InfContext;
    PCSTR FileName;
    ULONG FileSize;
    PVOID PhysicalBase;
    CHAR ErrataFilePath[MAX_PATH];

    /* Retrieve the INF file name value */
    if (!InfFindFirstLine(InfHandle, "BiosInfo", "InfName", &InfContext))
    {
        WARN("Failed to find 'BiosInfo/InfName'\n");
        return FALSE;
    }
    if (!InfGetDataField(&InfContext, 1, &FileName))
    {
        WARN("Failed to read 'InfName' value\n");
        return FALSE;
    }

    RtlStringCbCopyA(ErrataFilePath, sizeof(ErrataFilePath), SystemRoot);
    RtlStringCbCatA(ErrataFilePath, sizeof(ErrataFilePath), FileName);

    /* Load the INF file */
    PhysicalBase = WinLdrLoadModule(ErrataFilePath, &FileSize, LoaderRegistryData);
    if (!PhysicalBase)
    {
        WARN("Could not load '%s'\n", ErrataFilePath);
        return FALSE;
    }

    LoaderBlock->Extension->EmInfFileImage = PaToVa(PhysicalBase);
    LoaderBlock->Extension->EmInfFileSize  = FileSize;

    return TRUE;
}

static VOID
SetupLdrScanBootDrivers(PLIST_ENTRY BootDriverListHead, HINF InfHandle, PCSTR SearchPath)
{
    INFCONTEXT InfContext, dirContext;
    BOOLEAN Success;
    PCSTR Media, DriverName, dirIndex, ImagePath;
    WCHAR ServiceName[256];
    WCHAR ImagePathW[256];

    /* Open inf section */
    if (!InfFindFirstLine(InfHandle, "SourceDisksFiles", NULL, &InfContext))
        return;

    /* Load all listed boot drivers */
    do
    {
        if (InfGetDataField(&InfContext, 7, &Media) &&
            InfGetDataField(&InfContext, 0, &DriverName) &&
            InfGetDataField(&InfContext, 13, &dirIndex))
        {
            if ((strcmp(Media, "x") == 0) &&
                InfFindFirstLine(InfHandle, "Directories", dirIndex, &dirContext) &&
                InfGetDataField(&dirContext, 1, &ImagePath))
            {
                /* Convert name to widechar */
                swprintf(ServiceName, L"%S", DriverName);

                /* Prepare image path */
                swprintf(ImagePathW, L"%S", ImagePath);
                wcscat(ImagePathW, L"\\");
                wcscat(ImagePathW, ServiceName);

                /* Remove .sys extension */
                ServiceName[wcslen(ServiceName) - 4] = 0;

                /* Add it to the list */
                Success = WinLdrAddDriverToList(BootDriverListHead,
                                                L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\",
                                                ImagePathW,
                                                ServiceName,
                                                NULL,
                                                1,
                                                (ULONG)-1);
                if (!Success)
                {
                    ERR("Could not add boot driver '%s', '%s'\n", SearchPath, DriverName);
                    return;
                }
            }
        }
    } while (InfFindNextLine(&InfContext, &InfContext));
}


/* SETUP STARTER **************************************************************/

ARC_STATUS
LoadReactOSSetup(
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR Envp[])
{
    ARC_STATUS Status;
    PCSTR ArgValue;
    PCSTR SystemPartition;
    PCSTR SystemPath;
    PSTR FileName;
    ULONG FileNameLength;
    BOOLEAN BootFromFloppy;
    BOOLEAN Success;
    HINF InfHandle;
    INFCONTEXT InfContext;
    ULONG i, ErrorLine;
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    PSETUP_LOADER_BLOCK SetupBlock;
    CHAR BootPath[MAX_PATH];
    CHAR FilePath[MAX_PATH];
    CHAR BootOptions2[256];
    PSTR BootOptions;
    PCSTR LoadOptions;

    static PCSTR SourcePaths[] =
    {
        "", /* Only for floppy boot */
#if defined(_M_IX86)
        "I386\\",
#elif defined(_M_MPPC)
        "PPC\\",
#elif defined(_M_MRX000)
        "MIPS\\",
#endif
        "reactos\\",
        NULL
    };

    /* Retrieve the (mandatory) boot type */
    ArgValue = GetArgumentValue(Argc, Argv, "BootType");
    if (!ArgValue || !*ArgValue)
    {
        ERR("No 'BootType' value, aborting!\n");
        return EINVAL;
    }
    if (_stricmp(ArgValue, "ReactOSSetup") != 0)
    {
        ERR("Unknown 'BootType' value '%s', aborting!\n", ArgValue);
        return EINVAL;
    }

    /* Retrieve the (mandatory) system partition */
    SystemPartition = GetArgumentValue(Argc, Argv, "SystemPartition");
    if (!SystemPartition || !*SystemPartition)
    {
        ERR("No 'SystemPartition' specified, aborting!\n");
        return EINVAL;
    }

    UiDrawStatusText("Setup is loading...");

    UiDrawBackdrop();
    UiDrawProgressBarCenter(1, 100, "Loading ReactOS Setup...");

    /* Retrieve the system path */
    *BootPath = ANSI_NULL;
    ArgValue = GetArgumentValue(Argc, Argv, "SystemPath");
    if (ArgValue)
    {
        RtlStringCbCopyA(BootPath, sizeof(BootPath), ArgValue);
    }
    else
    {
        /*
         * IMPROVE: I don't want to use the SystemPartition here as a
         * default choice because I can do it after (see few lines below).
         * Instead I reset BootPath here so that we can build the full path
         * using the general code from below.
         */
        // RtlStringCbCopyA(BootPath, sizeof(BootPath), SystemPartition);
        *BootPath = ANSI_NULL;
    }

    /*
     * Check whether BootPath is a full path
     * and if not, create a full boot path.
     *
     * See FsOpenFile for the technique used.
     */
    if (strrchr(BootPath, ')') == NULL)
    {
        /* Temporarily save the boot path */
        RtlStringCbCopyA(FilePath, sizeof(FilePath), BootPath);

        /* This is not a full path: prepend the SystemPartition */
        RtlStringCbCopyA(BootPath, sizeof(BootPath), SystemPartition);

        /* Append a path separator if needed */
        if (*FilePath != '\\' && *FilePath != '/')
            RtlStringCbCatA(BootPath, sizeof(BootPath), "\\");

        /* Append the remaining path */
        RtlStringCbCatA(BootPath, sizeof(BootPath), FilePath);
    }

    /* Append a path separator if needed */
    if (!*BootPath || BootPath[strlen(BootPath) - 1] != '\\')
        RtlStringCbCatA(BootPath, sizeof(BootPath), "\\");

    TRACE("BootPath: '%s'\n", BootPath);

    /* Retrieve the boot options */
    *BootOptions2 = ANSI_NULL;
    ArgValue = GetArgumentValue(Argc, Argv, "Options");
    if (ArgValue && *ArgValue)
        RtlStringCbCopyA(BootOptions2, sizeof(BootOptions2), ArgValue);

    TRACE("BootOptions: '%s'\n", BootOptions2);

    /* Check if a RAM disk file was given */
    FileName = (PSTR)NtLdrGetOptionEx(BootOptions2, "RDPATH=", &FileNameLength);
    if (FileName && (FileNameLength > 7))
    {
        /* Load the RAM disk */
        Status = RamDiskInitialize(FALSE, BootOptions2, SystemPartition);
        if (Status != ESUCCESS)
        {
            FileName += 7; FileNameLength -= 7;
            UiMessageBox("Failed to load RAM disk file '%.*s'",
                         FileNameLength, FileName);
            return Status;
        }
    }

    /* Check if we booted from floppy */
    BootFromFloppy = strstr(BootPath, "fdisk") != NULL;

    /* Open 'txtsetup.sif' from any of the source paths */
    FileName = BootPath + strlen(BootPath);
    for (i = BootFromFloppy ? 0 : 1; ; i++)
    {
        SystemPath = SourcePaths[i];
        if (!SystemPath)
        {
            UiMessageBox("Failed to open txtsetup.sif");
            return ENOENT;
        }
        FileNameLength = (ULONG)(sizeof(BootPath) - (FileName - BootPath)*sizeof(CHAR));
        RtlStringCbCopyA(FileName, FileNameLength, SystemPath);
        RtlStringCbCopyA(FilePath, sizeof(FilePath), BootPath);
        RtlStringCbCatA(FilePath, sizeof(FilePath), "txtsetup.sif");
        if (InfOpenFile(&InfHandle, FilePath, &ErrorLine))
        {
            break;
        }
    }

    TRACE("BootPath: '%s', SystemPath: '%s'\n", BootPath, SystemPath);

    /* Get load options - debug and non-debug */
    if (!InfFindFirstLine(InfHandle, "SetupData", "OsLoadOptions", &InfContext))
    {
        ERR("Failed to find 'SetupData/OsLoadOptions'\n");
        return EINVAL;
    }

    if (!InfGetDataField(&InfContext, 1, &LoadOptions))
    {
        ERR("Failed to get load options\n");
        return EINVAL;
    }

#if DBG
    /* Get debug load options and use them */
    if (InfFindFirstLine(InfHandle, "SetupData", "DbgOsLoadOptions", &InfContext))
    {
        PCSTR DbgLoadOptions;

        if (InfGetDataField(&InfContext, 1, &DbgLoadOptions))
            LoadOptions = DbgLoadOptions;
    }
#endif

    /* Copy LoadOptions (original string will be freed) */
    BootOptions = FrLdrTempAlloc(strlen(LoadOptions) + 1, TAG_BOOT_OPTIONS);
    ASSERT(BootOptions);
    strcpy(BootOptions, LoadOptions);

    TRACE("BootOptions: '%s'\n", BootOptions);

    /* Allocate and minimally-initialize the Loader Parameter Block */
    AllocateAndInitLPB(_WIN32_WINNT_WS03, &LoaderBlock);

    /* Allocate and initialize the setup loader block */
    SetupBlock = &WinLdrSystemBlock->SetupBlock;
    LoaderBlock->SetupLdrBlock = SetupBlock;

    /* Set textmode setup flag */
    SetupBlock->Flags = SETUPLDR_TEXT_MODE;

    /* Load the "setupreg.hiv" setup system hive */
    UiDrawBackdrop();
    UiDrawProgressBarCenter(15, 100, "Loading setup system hive...");
    Success = WinLdrInitSystemHive(LoaderBlock, BootPath, TRUE);
    TRACE("Setup SYSTEM hive %s\n", (Success ? "loaded" : "not loaded"));
    /* Bail out if failure */
    if (!Success)
        return ENOEXEC;

    /* Load NLS data, they are in the System32 directory of the installation medium */
    RtlStringCbCopyA(FilePath, sizeof(FilePath), BootPath);
    RtlStringCbCatA(FilePath, sizeof(FilePath), "system32\\");
    SetupLdrLoadNlsData(LoaderBlock, InfHandle, FilePath);

    /* Load the Firmware Errata file from the installation medium */
    Success = SetupLdrInitErrataInf(LoaderBlock, InfHandle, BootPath);
    TRACE("Firmware Errata file %s\n", (Success ? "loaded" : "not loaded"));
    /* Not necessarily fatal if not found - carry on going */

    // UiDrawStatusText("Press F6 if you need to install a 3rd-party SCSI or RAID driver...");

    /* Get a list of boot drivers */
    SetupLdrScanBootDrivers(&LoaderBlock->BootDriverListHead, InfHandle, BootPath);

    /* Close the inf file */
    InfCloseFile(InfHandle);

    UiDrawStatusText("The Setup program is starting...");

    /* Finish loading */
    return LoadAndBootWindowsCommon(_WIN32_WINNT_WS03,
                                    LoaderBlock,
                                    BootOptions,
                                    BootPath);
}
