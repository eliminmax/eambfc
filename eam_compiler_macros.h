/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file defines macros for use in the compilation process, as a more
 * readable alternative to just having byte values with no clear meaning. */
#ifndef EAMBFC_COMPILER_MACROS_H
#define EAMBFC_COMPILER_MACROS_H 1

/* assorted macros for the size/address of different elements in the ELF file */

/* virtual memory address of the tape - cannot overlap with the machine code.
 * chosen fairly arbitrarily. */
#define TAPE_ADDRESS 0x10000

/* number of entries in program and section header tables respectively */
#define PHNUM 2
#define SHNUM 0

/* size of the Ehdr struct, once serialized. */
#define EHDR_SIZE 64
/* Sizes of Phdr table entries and Shdr table entries, respectively */
#define PHDR_SIZE 56
#define SHDR_SIZE 512
/* sizes of the actual tables themselves */
#define PHTB_SIZE PHNUM * PHDR_SIZE
#define SHTB_SIZE SHNUM * SHDR_SIZE

/* TAPE_BLOCKS is defined at a static level in eam_compile.c */
# define TAPE_SIZE(tape_blocks) (tape_blocks * 0x1000)

/* virtual address of the section containing the machine code
 * should be after the tape ends to avoid overlapping with the tape.
 *
 * Zero out the lowest 2 bytes of the end of the tape and add 0x10000 to ensure
 * that there is enough room. */

#define LOAD_VADDR(tape_blocks) \
    (((TAPE_ADDRESS + TAPE_SIZE(tape_blocks)) & (~ 0xffff)) + 0x10000)

/* physical address of the starting instruction
 * use the same technique as LOAD_VADDR to ensure that it is at a 256-byte
 * boundary. */
#define START_PADDR (((((EHDR_SIZE + PHTB_SIZE)) & ~ 0xff) + 0x100))

/* virtual address of the starting instruction */
#define START_VADDR(ts) (START_PADDR + LOAD_VADDR(ts))

/* The offset within the file for the program and section header tables
 * respectively. If there are no entries, they should be set to 0. */
/* program header table is right after the ELF header. */
#define PHOFF (PHNUM ? EHDR_SIZE : 0)

/* section header table offset is 0, as there is no section header table. */
#define SHOFF 0

/* out_sz must be defined as the size in bytes of the machine code.
 * FILE_SIZE should not be used until after its final value is known.
 * It is the size (in bytes) of the file. */
#define FILE_SIZE(out_sz) (START_PADDR + out_sz)

/* the memory address of the current instruction. */
#define CURRENT_ADDRESS(out_sz) (START_PADDR + out_sz)

#endif /* EAMBFC_COMPILER_MACROS_H */
