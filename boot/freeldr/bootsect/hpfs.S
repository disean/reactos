/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     HPFS volume boot sector
 * COPYRIGHT:   Copyright 2022 Dmitry Borisov (di.sean@protonmail.com)
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>
#include <freeldr/include/arch/pc/x86common.h>

/* GLOBALS *******************************************************************/

SizeOfDataArea        = 32

BootSectorStackTop    = (HEX(7c00) - SizeOfDataArea)

BiosCHSDriveSize      = BootSectorStackTop + 0
DiskBufferOffset      = BootSectorStackTop + 4
DiskBufferSegment     = BootSectorStackTop + 6

#define BP_REL(x)   ss:[bp + x - BootSectorStackTop]

#define FILENAME_LENGTH   11

// org 7c00h

.code16

/* BPB DATA *******************************************************************/

start:
    jmp short main
    nop
OEMName:
    .ascii "FrLdr1.0"
BytesPerSector:
    .word 512
SectorsPerCluster:
    .byte 0
ReservedSectors:
    .word 1
NumberOfFats:
    .byte 0
MaxRootEntries:
    .word 512
TotalSectors:
    .word 0
MediaDescriptor:
    .byte HEX(0F8)
SectorsPerFat:
    .word 0
SectorsPerTrack:
    .word 0
NumberOfHeads:
    .word 0
HiddenSectors:
    .long 0
TotalSectorsBig:
    .long 0
BootDrive:
    .byte HEX(0FF)
Reserved:
    .byte 0
ExtendSig:
    .byte HEX(28)
SerialNumber:
    .long 0
VolumeLabel:
    .ascii "NO NAME    "
FileSystem:
    .ascii "HPFS    "

/* ENTRYPOINT *****************************************************************/

main:
    /* First setup the segment registers */
    xor eax, eax
    mov ds, ax
    mov es, ax
    mov ss, ax

    /* Setup a stack */
    mov sp, BootSectorStackTop

    mov bp, sp

    mov al, byte ptr ds:[BootDrive]

    /* Check if the boot drive is valid */
    cmp al, HEX(0FF)
    je .SaveBootDrive

    mov dl, al

.SaveBootDrive:
    /* Save the boot drive in the BPB */
    mov byte ptr ds:[BootDrive], dl

    /* Get the drive parameters */
    xor di, di
    mov ah, 8
    int HEX(13)
    jnc .CalcDriveSize

    /* If the call to the BIOS failed set CHS equal to the maximum addressable size */
    mov cx, HEX(0FFFF)
    mov dh, cl

.CalcDriveSize:
    mov bl, ch      // Put the low 8-bits of the cylinder count into BL
    mov bh, cl      // Put the high 2-bits in BH
    shr bh, 6       // Shift them into position, now BX contains the cylinder count

    and cl, HEX(3F) // Mask off cylinder bits from sector count
    movzx ecx, cl   // Move the sectors per track into ECX

    movzx ebx, bx   // Move the cylinders into EBX
    movzx eax, dh   // Move the heads into EAX

    inc eax         // Make it one based because the BIOS returns it zero based
    inc ebx         // Make the cylinder count one based also
    mul ecx         // Multiply heads with the sectors per track, result in EDX:EAX
    mul ebx         // Multiply the cylinders with (heads * sectors) [stored in EDX:EAX already]

    /* We now have the total number of sectors as reported by the BIOS */
    mov dword ptr BP_REL(BiosCHSDriveSize), eax

    xor eax, eax

    /* We have to load our extra boot code at next sector into memory at [0000:7E00h] */
    mov al, 1
    mov cx, 1
    mov bx, HEX(7E0)
    mov es, bx
    xor di, di
    call ReadSectors

    /* Read the SuperBlock into [0000:8000h] */
    push es
    mov al, 16
    inc cx
    call ReadSectors

    /* Check the magic */
    pop es
    mov si, offset SuperBlockSignature
    mov cx, 4
    repe cmpsw
    je .SignatureOk

    mov si, offset msgSignatureError
    jmp PrintCustomError

.SignatureOk:
    /* Read the Root Directory */
    push es
    xor di, di
    mov eax, dword ptr es:[HEX(0C)]
    mov cx, 1
    call ReadSectors
    pop es
    push es
    mov eax, dword ptr es:[HEX(48)]
    mov cx, 4
    call ReadSectors

    jmp StartSearch

/*
 * Displays a disk error message and reboots
 */
PrintDiskError:
    mov si, offset msgDiskError

PrintCustomError:
    call PutChars

/*
 * Reboots the computer after keypress
 */
Reboot:
    /* Press any key message */
    mov si, offset msgAnyKey
    call PutChars

    xor ax, ax

    /* Wait for a keypress */
    int HEX(16)

    /* Reboot */
    int HEX(19)

.PutCharsLoop:
    mov ah, HEX(0E)
    mov bx, 7
    int HEX(10)
/*
 * Prints a null-terminated string
 *
 * Call with:
 * DS:SI - Pointer to a string
 */
PutChars:
    lodsb
    or al, al
    jnz .PutCharsLoop
    ret

/*
 * Reads logical sectors
 *
 * Call with:
 *  ES:DI   - Buffer
 *  EAX     - Logical sector number to read
 *  CX      - Number of sectors to read
 * Modifies:
 *  EAX     - Incremented by sector count
 *  CX      - 0
 *  ES      - Incremented
 * Clobbers: EBX
 */
ReadSectors:
    /* Add offset from the disk beginning */
    add eax, dword ptr BP_REL(HiddenSectors)

    /* Check if they are reading a sector outside CHS range */
    cmp eax, dword ptr BP_REL(BiosCHSDriveSize)
    jae .ReadSectorsLbaLoop

    pushad

    /* Now check if this computer supports extended reads */
    mov ah, HEX(41)                 // AH = 41h
    mov bx, HEX(55AA)               // BX = 55AAh
    mov dl, byte ptr ds:[BootDrive] // DL = drive (80h-FFh)
    int HEX(13)                     // IBM/MS INT 13 Extensions - INSTALLATION CHECK
    jc .ReadSectorsChs              // CF set on error (extensions not supported)
    cmp bx, HEX(0AA55)              // BX = AA55h if installed
    jne .ReadSectorsChs
    test cl, 1                      // CX = API subset support bitmap
    jz .ReadSectorsChs              // Bit 0, extended disk access functions
                                    // (AH=42h-44h,47h,48h) supported

    popad

.ReadSectorsLbaLoop:
    /* Copy number of sectors to EBX */
    movzx ebx, cx

    /* Since the LBA calls only support 0x7F sectors at a time we will limit ourselves to 64 */
    cmp bx, 64
    jbe .ReadSectorsSetupDiskAddressPacket
    mov bx, 64

.ReadSectorsSetupDiskAddressPacket:
    pushad

    data32 push 0 // Put 64-bit logical block address (high part) on stack
    push eax      // Put 64-bit logical block address (low part) on stack
    push es       // Put transfer segment on stack
    push di       // Put transfer offset on stack
    push bx       // Set transfer count
    push 16       // Set size of packet to 16

    /* Setup the disk address packet on the stack */
    mov si, sp

    /* Drive number */
    mov dl, byte ptr ds:[BootDrive]

    /* INT 13h, AH = 42h - Extended Read */
    mov ah, HEX(42)
    int HEX(13)

    jc PrintDiskError

    /* Remove disk address packet from stack */
    add sp, 16

    /* Setup read buffer for next sector */
    shl bx, 5
    mov ax, es
    add ax, bx
    mov es, ax

    popad

    /* Increment sector to read */
    add eax, ebx

    /* Read next sectors */
    sub cx, bx
    jnz .ReadSectorsLbaLoop

    ret

.ReadSectorsChs:
    popad

.ReadSectorsChsLoop:
    pushad

    xor edx, edx
    movzx ecx, word ptr BP_REL(SectorsPerTrack)

    /* Divide logical by SectorsPerTrack */
    div ecx
    inc dl
    mov cl, dl

    /* Divide logical by number of heads */
    mov edx, eax
    shr edx, 16
    div word ptr BP_REL(NumberOfHeads)
    mov dh, dl
    mov ch, al

    /* Low 8 bits of cylinder in CH, high 2 bits in CL shifted to bits 6 & 7 */
    ror ah, 2

    /* Add the sector number */
    or cl, ah

    /* Buffer offset */
    mov bx, di

    /* Drive number */
    mov dl, byte ptr ds:[BootDrive]
    mov ax, HEX(0201)

    int HEX(13)
    jc PrintDiskError

    popad

    /* Increment sector to read */
    inc eax

    /* Setup read buffer for next sector */
    mov bx, es
    add bx, 32
    mov es, bx

    loop .ReadSectorsChsLoop

    ret

msgDiskError:
    .ascii "Disk error", CR, LF, NUL

msgAnyKey:
    .ascii "Press any key to reboot...", NUL

    /* Pad to 509 bytes */
    .org 509

BootPartition:
    .byte 0

BootSignature:
    .word HEX(0AA55)  // BootSector signature

/* NEXT SECTOR ****************************************************************/

StartSearch:
    /* Restore the address of the node */
    pop ds

    /* First key in the node */
    mov bx, HEX(14)

.SearchTreeKeyLoop:
    /* We encountered the end of the node */
    test byte ptr ds:[bx + 2], 8
    jnz .SearchTreeExploreNextLevel

    /* Check the key */
    mov cl, byte ptr ds:[bx + HEX(1E)]
    lea si, [bx + HEX(1F)]
    call CompareKeys
    jb .SearchTreeNextKey
    je FoundFreeLoader

.SearchTreeExploreNextLevel:
    /* Has a pointer to the subtree */
    test byte ptr ds:[bx + 2], 4
    jz PrintFileNotFound

    /* Read the next node */
    add bx, word ptr ds:[bx]
    mov eax, dword ptr ds:[bx - 4]
    mov cx, 4
    push ds
    push ds
    pop es
    xor di, di
    mov ds, di
    call ReadSectors

    jmp StartSearch

.SearchTreeNextKey:
    /* Next key */
    add bx, word ptr ds:[bx]
    jmp .SearchTreeKeyLoop

/*
 * Call with:
 *  DS:SI - Pointer to a string
 *  CL    - String length
 */
CompareKeys:
    /* Put offset of the filename string in ES:DI */
    push 0
    pop es
    mov di, offset filename

    mov ch, cl

    /* Set the length for the comparsion */
    cmp cl, FILENAME_LENGTH
    jbe .CompareKeysLoop

    mov cl, FILENAME_LENGTH

.CompareKeysLoop:
    lodsb

    /* Do case-insensitive string comparison */
    mov dl, al
    sub dl, 'a'
    cmp dl, 'z' - 'a'
    ja .CompareKeysUpperCase
    xor al, HEX(20)

.CompareKeysUpperCase:
    scasb
    jne .CompareKeysDone

    dec cl
    jnz .CompareKeysLoop

    /* Set the flags */
    sub ch, FILENAME_LENGTH

.CompareKeysDone:
    ret

PrintFileNotFound:
    /* TODO */
    push 0
    pop ds

    mov si, offset msgFreeLdr
    jmp PrintCustomError

PrintUnimplemented:
    /* TODO */
    push 0
    pop ds

    mov si, offset msgUnimplementedError
    jmp PrintCustomError

FoundFreeLoader:
    /* Read the parent Fnode */
    mov eax, dword ptr ds:[bx + 4]
    mov cx, 1
    push ds
    push ds
    pop es
    xor di, di
    mov ds, di
    call ReadSectors
    pop ds

    /* TODO */
    test byte ptr ds:[HEX(38)], 4
    jnz PrintUnimplemented

    /* TODO */
    push ds
    push 0
    pop ds

    /* Display the loading message */
    mov si, offset msgLoading
    call PutChars

    pop ds

    /* Set the buffer address */
    mov dx, FREELDR_BASE / 16
    mov es, dx
    xor di, di

    /* First run in the Fnode */
    mov si, HEX(40)
    xor bx, bx
    mov bl, byte ptr ds:[HEX(3D)]

ReadRunLoop:
    /* Load the physical address */
    mov eax, dword ptr ds:[si + 8]

    /* Load the run size */
    mov ecx, dword ptr ds:[si + 4]

    /* Read the run */
    push ds
    push ds
    push 0
    pop ds
    push bx
    call ReadSectors
    pop bx
    pop ds

    /* Next run */
    lea si, [si + HEX(0C)]

    dec bl
    jnz ReadRunLoop

    /* Transfer control to FreeLoader */
    xor bx, bx
    mov ds, bx
    mov es, bx
    mov dl, byte ptr ds:[BootDrive]
    mov dh, byte ptr ds:[BootPartition]
    ljmp16 0, FREELDR_BASE

SuperBlockSignature:
    .quad HEX(FA53E9C5F995E849)

filename:
    .ascii "FREELDR.SYS"

msgUnimplementedError:
    .ascii "UNIMPLEMENTED", CR, LF, NUL

msgSignatureError:
    .ascii "Signature error", CR, LF, NUL

msgFreeLdr:
    .ascii "freeldr.sys not found", CR, LF, NUL

msgLoading:
    .ascii "Loading FreeLoader...", CR, LF, NUL

.endcode16

END
