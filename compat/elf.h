/* SPDX-FileCopyrightText: 1995-2022 Free Software Foundation, Inc.
 * SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains some macros and typedefs from the GNU C Library's `elf.h`,
 * to allow eambfc to be compiled on systems that do not provide an `elf.h`
 * header file.
 *
 * Only those needed for eambfc were retained, and comments were edited to
 * reflect that.
 *
 * Code was refactord, and reformatted to fit my preferred style of C code.
 *
 * The unmodified GNU C Library's elf.h file can be used in place of this, as
 * can the FreeBSD sys/elf64.h file. Simply replace `#include "compat/elf.h"`
 * with `#include <elf.h>` for the former, or `#include <sys/elf64.h>` for the
 * latter. Doing so is neither necessary nor beneficial, as this file will still
 * be available, but it is still an option. */

/* This file defines standard ELF types, structures, and macros.
   Copyright (C) 1995-2022 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 3.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

#ifndef _ELF_H
#define _ELF_H 1

#include "eambfc_inttypes.h" /* uint*_t */

/* Standard ELF types.  */

typedef uint16_t Elf64_Half;    /* Type for a 16-bit quantity. */
typedef uint32_t Elf64_Word;    /* Type for an unsigned 32-bit quantity. */
typedef uint64_t Elf64_Xword;   /* Type for an unsigned 64-bit quantity. */
typedef uint64_t Elf64_Addr;    /* Type of addresses.  */
typedef uint64_t Elf64_Off;     /* Type of file offsets.  */

/* The ELF file header.  This appears at the start of every ELF file.  */

#define EI_NIDENT 16

typedef struct {
    unsigned char e_ident[EI_NIDENT]; /* Magic number and other info */
    Elf64_Half    e_type;             /* Object file type */
    Elf64_Half    e_machine;          /* Architecture */
    Elf64_Word    e_version;          /* Object file version */
    Elf64_Addr    e_entry;            /* Entry point virtual address */
    Elf64_Off     e_phoff;            /* Program header table file offset */
    Elf64_Off     e_shoff;            /* Section header table file offset */
    Elf64_Word    e_flags;            /* Processor-specific flags */
    Elf64_Half    e_ehsize;           /* ELF header size in bytes */
    Elf64_Half    e_phentsize;        /* Program header table entry size */
    Elf64_Half    e_phnum;            /* Program header table entry count */
    Elf64_Half    e_shentsize;        /* Section header table entry size */
    Elf64_Half    e_shnum;            /* Section header table entry count */
    Elf64_Half    e_shstrndx;         /* Section header string table index */
} Elf64_Ehdr;

/* Fields in the e_ident array.  The EI_* macros are indices into the
 * array.  The macros under each EI_* macro are the values the byte
 * may have.  */

#define EI_MAG0       0    /* File identification byte 0 index */
#define ELFMAG0       0x7f /* Magic number byte 0 */

#define EI_MAG1       1    /* File identification byte 1 index */
#define ELFMAG1       'E'  /* Magic number byte 1 */

#define EI_MAG2       2    /* File identification byte 2 index */
#define ELFMAG2       'L'  /* Magic number byte 2 */

#define EI_MAG3       3    /* File identification byte 3 index */
#define ELFMAG3       'F'  /* Magic number byte 3 */

#define EI_CLASS      4    /* File class byte index */
#define ELFCLASS64    2    /* 64-bit objects */

#define EI_DATA       5    /* Data encoding byte index */
#define ELFDATA2LSB   1    /* 2's complement, little endian */

#define EI_VERSION    6    /* File version byte index */
                           /* Value must be EV_CURRENT */

#define EI_OSABI      7    /* OS ABI identification */
#define ELFOSABI_SYSV 0    /* UNIX System V ABI */

#define EI_ABIVERSION 8    /* ABI version */

#define EI_PAD        9    /* Byte index of padding bytes */

/* value used for e_type (object file type). */
#define ET_EXEC       2    /* Executable file */
/* value used for e_machine (architecture).  */
#define EM_X86_64     62   /* AMD x86-64 architecture */
/* value used for e_version (version).  */
#define EV_CURRENT    1    /* Current version */
/* value used for e_shstrndx (SHDR table index of name string table section) */
#define SHN_UNDEF     0   /* there is no name string table */

/* Program segment header.  */

typedef struct {
    Elf64_Word  p_type;   /* Segment type */
    Elf64_Word  p_flags;  /* Segment flags */
    Elf64_Off   p_offset; /* Segment file offset */
    Elf64_Addr  p_vaddr;  /* Segment virtual address */
    Elf64_Addr  p_paddr;  /* Segment physical address */
    Elf64_Xword p_filesz; /* Segment size in file */
    Elf64_Xword p_memsz;  /* Segment size in memory */
    Elf64_Xword p_align;  /* Segment alignment */
} Elf64_Phdr;

/* value used for p_type (segment type).  */

#define PT_LOAD 1           /* Loadable program segment */

/* values used for p_flags (segment flags).  */

#define PF_X    1    /* Segment is executable */
#define PF_W    2    /* Segment is writable */
#define PF_R    4    /* Segment is readable */

#endif  /* elf.h */
