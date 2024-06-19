/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file defines macros for use in the compilation process, as a more
 * readable alternative to just having byte values with no clear meaning. */
#ifndef EAM_COMPILER_MACROS_H
#define EAM_COMPILER_MACROS_H 1
/* C99 */
#include <stdint.h>
/* POSIX */
#include <sys/types.h>
/* internal */
#include "config.h"
#include "compat/elf.h"

/* the Linux kernel reads system call numbers from RAX on x86_64 systems,
 * and reads arguments from RDI, RSI, RDX, R10, R8, and R9.
 * None of the system calls needed use more than 3 arguments, and the R8-R15
 * registers are addressed incompatibly, so only worry the first 3 argument
 * registers.
 *
 * the RBX register is preserved through system calls, so it's useful as the
 * tape pointer.
 *
 * Thus, for eambfc, the registers to care about are RAX, RDI, RSI, RDX, and RBX
 *
 * Oversimpifying a bit, in x86 assembly, when specifying a register that is not
 * one of R8-R15, a 3-bit value is used to identify it.
 *
 * * RAX is 000b
 * * RDI is 111b
 * * RSI is 110b
 * * RDX is 010b
 * * RBX is 011b
 *
 * Because one octal symbol represents 3 bits, octal is used for the macro
 * definitions.
 * */
#define REG_SC_NUM      00 /* RAX */
#define REG_ARG1        07 /* RDI */
#define REG_ARG2        06 /* RSI */
#define REG_ARG3        02 /* RDX */
#define REG_BF_PTR  03 /* RBX */


/* A couple of macros that are useful for various purposes */

/* test and jump are associated with each other. */
#define JUMP_SIZE 9

/* assorted macros for the size/address of different elements in the ELF file */

/* virtual memory address of the tape - cannot overlap with the instructions
 * chosen fairly arbitrarily. */
#define TAPE_ADDRESS 0x10000

/* Temporary value for use when initially creating the structure and control
 * flow of this file. */
#define PLACEHOLDER 0

/* number of entries in program and section header tables respectively */
#define PHNUM 2
#define SHNUM 0

/* sizes of the Ehdr, Phdr table, and Shdr table */
#define EHDR_SIZE 64
#define PHDR_SIZE 56
#define SHDR_SIZE 512
#define PHTB_SIZE PHNUM * PHDR_SIZE
#define SHTB_SIZE SHNUM * SHDR_SIZE

/* TAPE_BLOCKS is defined in config.h */
# define TAPE_SIZE (TAPE_BLOCKS * 0x1000)

/* virtual address of the section containing the machine code
 * should be after the tape ends to avoid overlapping with the tape.
 *
 * Zero out the lowest 2 bytes of the end of the tape and add 0x10000 to ensure
 * that there is enough room. */

#define LOAD_VADDR (((TAPE_ADDRESS + TAPE_SIZE) & (~ 0xffff)) + 0x10000)

/* physical address of the starting instruction
 * use the same technique as LOAD_VADDR to ensure that it is at a 256-byte
 * boundry. */
#define START_PADDR (((((EHDR_SIZE + PHTB_SIZE)) & ~ 0xff) + 0x100))

/* virtual address of the starting instruction */
#define START_VADDR (START_PADDR + LOAD_VADDR)

/* The offset within the file for the program and section header tables
 * respectively. If there are no entries, they should be set to 0. */
/* program header table is right after the ELF header. */
#define PHOFF (PHNUM ? EHDR_SIZE : 0)

/* section header table offset is 0, as there is no section header table. */
#define SHOFF 0

/* out_sz must be defined as the size in bytes of the machine code.
 * FILE_SIZE should not be used until after its final value is known.
 * It is the size (in bytes) of the file. */
#define FILE_SIZE (START_PADDR + out_sz)

/* the memory address of the current instruction. */
#define CURRENT_ADDRESS (START_PADDR + out_sz)

/* Linux system call numbers */

#define SYSCALL_READ 0
#define SYSCALL_WRITE 1
#define SYSCALL_EXIT 60

#endif /* EAM_COMPILER_MACROS_H */
