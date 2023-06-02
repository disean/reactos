/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     OLPC XO-1 StartUp Code
 * COPYRIGHT:   Copyright 2023 Dmitry Borisov (di.sean@protonmail.com)
 */

#include <asm.inc>

#include <arch/pc/x86common.h>
#include <multiboot.h>

.code32

START_ROM_SIZE = FREELDR_PE_BASE - FREELDR_BASE
FILE_SIZE = START_ROM_SIZE + 460216 // sizeof(freeldr_pe.exe) FIXME: Must be a compile-time constant
LOAD_ADDRESS = (FREELDR_BASE + (_start - ElfHeader))

    /* ELF Header */
ElfHeader:
    .byte HEX(07F)
    .ASCII "ELF"
    .byte HEX(01) // 32-bit
    .byte HEX(01) // 2LSB
    .byte HEX(01) // CURRENT
    .space 16 - ($ - ElfHeader), 0 // Pad to 16 bytes
e_type:
    .word 2       // EXEC
e_machine:
    .word 3       // 386
e_version:
    .long 1
e_entry:
    .long LOAD_ADDRESS
e_phoff:
    .long ProgramHeader - ElfHeader
e_shoff:
    .long 0
e_flags:
    .long 0
e_ehsize:
    .word ElfHeaderEnd - ElfHeader
e_phentsize:
    .word ProgramHeaderEnd - ProgramHeader
e_phnum:
    .word 1
e_shentsize:
    .word 0
e_shnum:
    .word 0
e_shstrndx:
    .word 0
ElfHeaderEnd = $

    /* Program Header */
ProgramHeader:
    .long HEX(01) // LOAD
p_offset:
    .long _start - ElfHeader
p_vaddr:
    .long LOAD_ADDRESS
p_paddr:
    .long LOAD_ADDRESS
p_filesz:
    .long FILE_SIZE
p_memsz:
    .long FILE_SIZE
p_flags:
    .long HEX(7)  // X, W, R
p_align:
    .long 4
ProgramHeaderEnd = $

.align 4

    /* Entry point */
_start:
    push ebp
    mov ebp, esp
    sub esp, 4

    /* Save the OFW ponter */
    mov [ebp - 4], eax

    /*
     * FIXME: It isn't necessary to call the multiboot entry point.
     * See https://github.com/HBelusca/reactos/commits/freeldr_startrom
     */

    /* Load the address and align it to 32 bits boundary */
    mov esi, FREELDR_PE_BASE
    and esi, -4

.MultibootHeaderFind:
    mov eax, [esi]
    cmp eax, MULTIBOOT_HEADER_MAGIC
    jne .MultibootHeaderNext

    /* Header flags */
    add eax, [esi + HEX(04)]

    /* Header checksum */
    add eax, [esi + HEX(08)]

    jz .MultibootHeaderFound

.MultibootHeaderNext:
    add esi, 4
    cmp esi, FREELDR_PE_BASE + HEX(2000)
    jb .MultibootHeaderFind

    /* Not found */
    int 1
stop:
    jmp short stop

.MultibootHeaderFound:
    /* Entry address */
    mov ecx, [esi + HEX(1C)]

    /* Header address */
    sub ecx, [esi + HEX(0C)]

    /* Multiboot entry point */
    add ecx, esi

    /* Magic value for FreeLoader */
    mov eax, HEX(4F4C5043)

    /* OFW ponter */
    mov esi, [ebp - 4]

    /* Jump to the multiboot entry point */
    push ecx
    ret

.org (START_ROM_SIZE - 1)
.byte 0

END
