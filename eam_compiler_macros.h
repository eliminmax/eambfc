/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * */
#ifndef EAMASM_MACROS
#define EAMASM_MACROS
/* C99 */
#include <stdint.h>
/* POSIX */
#include <sys/types.h>
/* internal */
#include "elf.h"

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
#define REG_BF_POINTER  03 /* RBX */


/* A couple of macros that are useful for various purposes */

/* Add somewhere from 1 to 256 bytes to a memory address to ensure it's
 * 256-byte-aligned and separated from whatever came before it by at least 1
 * byte. */
#define padTo256(address) (((address & ~ 0xff) + 0x100))

/* use some bitwise nonsense to rewrite a double word (4-byte value) into 4
 * separate byte values, in little-endian order. */
#define dwordToBytes(d) \
    (d & 0x000000ff), /* smallest byte */ \
    (d & 0x0000ff00) >> 8, /* 2nd-smallest byte */ \
    (d & 0x00ff0000) >> 16, /* 2nd-largest byte */ \
    (d & 0xff000000) >> 24 /* largest byte */

/* the following are macros to make the machine code more readable, almost like
 * subset of an assembly language, though I use different mnemonics that I
 * believe better reflect the way the instructions are used within eambfc. */

/* Copy the value of a source register to a destination register
 *
 * explanation:
 * 89 c0+(src<<3)+dst copies the contents of the register dst to src
 *
 * equivalent to MOV src, dst */
#define eamasm_regcopy(dst, src) 0x89, 0xc0 + (src << 3) + dst

/* The system call instruction, in all of its glory. */
#define eamasm_syscall() 0x0f, 0x05

/* set various CPU flags based on the byte pointed to by reg
 * equivalent to TEST byte [reg], 0xff */
#define eamasm_jump_test(reg) 0xf6, reg, 0xff

/* jump by jump_offset bytes if the memory address pointed to by reg isn't 0 */
#define eamasm_jump_not_zero(reg, jump_offset) \
    eamasm_jump_test(reg),\
    0x0f, 0x85, dwordToBytes(jump_offset) /* equivalent to JNZ jump_offset */

/* jump by jump_offset bytes if the memory address pointed to by reg == 0 */
#define eamasm_jump_zero(reg, jump_offset) \
    eamasm_jump_test(reg),\
    0x0f, 0x84, dwordToBytes(jump_offset) /* equivalent to JZ jump_offset */

/* the size of the jump instructions, in bytes. */
#define TEST_INSTRN_SIZE 3
#define JUMP_INSTRN_SIZE 6

/* test and jump are associated with each other. */
#define JUMP_SIZE (TEST_INSTRN_SIZE + JUMP_INSTRN_SIZE)

/* Increment or decrement a byte address in a register or a register's contents
 * the brainfuck instructions "<", ">", "+", and "-" can all be implemented
 * with a few variations of the INC and DEC instructions.
 *
 * INC and DEC can either operate on a value stored in a register, or a memory
 * address pointed to by a register. Either way, they take a register as an
 * argument.
 *
 * the forms of the instruction used here are all 16 bits long, set as follows:
 *
 * bits 00..06 are set to 1
 * bits 07..09 are set to 1 if operating on a register, 0 otherwise
 * bits 10..11 are set to 0
 * bit 12 is set to 1 if decrementing, 0 otherwise
 * bits 13..15 are set to the register identifier
 *
 * dir is one of OFFDIR_POSITIVE or OFFDIR_NEGATIVE
 * adr is one of OFFMODE_MEMORY or OFFMODE_REGISTER
 * reg is the definition register */
#define eamasm_offset(dir, adr, reg) (0xfe|(adr&1)), (dir|reg|(adr<<6))
/* macros for use as arguments to eamasm_offset */
#define OFFMODE_MEMORY 0
#define OFFMODE_REGISTER 3
#define OFFDIR_POSITIVE 0
#define OFFDIR_NEGATIVE 8
/* Using 3 for OFFMODE_REGISTER enables some cursed bitwise manipulation to use
 * it to set the lowest bit of the first byte and top 2 bits of the second byte.
 *
 * Similarly, using 8 for OFFDIR_NEGATIVE enables some bitwise efficiency, but
 * it's far cleaner.
 *
 * Because it's one of my first dives into bitwise operations, I want to
 * document it in excessive detail.
 *
 * I'll walk through the macro with the following args (used for brainfuck ">"):
 *      eamasm_offset(OFFDIR_POSITIVE, OFFMODE_REGISTER, REG_BF_POINTER)
 *
 * * OFFDIR_POSITIVE->0, OFFDIR_REGISTER->3, REG_BF_POINTER->03
 * * (0xfe|(3&1)), in binary, is (0b1111_1110|(0b0000_0011 & 0b0000_0001))
 * * (0b0000_0011 & 0b0000_0001) evaluates to 0000_0001
 * * (0b1111_1110 | 0b0000_0001) evaluates to 0b1111_1111 (i.e. 0xff)
 *
 * * (3<<6) means 3, bit shifted left 6 times. 3 in binary is 0b11, and shifting
 * * that left 6 times results in 0b1100_0000, or 0xc0.
 * * (0|03|(3<<6)), is (0|03|0xc0), or (0b0000_0000|0b0000_0011|0b1100_0000).
 * * (0b0000_0000|0b0000_0011|0b1100_0000) evaluates to 0b1100_0011 (i.e. 0xc3)
 * * thus, the eamasm_offset example evaluates to 0xff, 0xc3.
 *
 * * ffc3 disassembles (with rasm2) to `inc ebx`.
 *
 * * rbx is REG_BF_POINTER's 64-bit form, but given that it can't exceed the
 * * 32-bit unsigned integer limit in eambfc, using its 32-bit form (ebx) is ok.
 * * Given that it saves a byte, might as well go with it.
 * */

/* Set a register (dst) to a 1 byte value (b).
 *
 * explanation:
 * 6a ib pushes a 1-byte immediate (ib) to the stack
 * 58+rd pops a value from the stack, saving it to a register (rd)
 * this is shorter than MOV rd, ib
 *
 * equivalent to PUSH b; POP dst
 * */
#define eamasm_setregb(dst, b) 0x6a, b, 0x58 + dst

/* Set a register (dst) to a double word value (d).
 * In x86 terminology, a word is 2 bytes and a double word is 4 bytes.
 *
 * explanation:
 * b8+rd, followed by an LSB-encoded 4 byte value, stores the 4 byte value in rd
 *
 * equivalent to MOV dst, d */
#define eamasm_setregd(dst, d) (0xb8 + (dst)), dwordToBytes(d)

/* assorted macros for the size/address of different elements in the ELF file */

/* the tape size in Urban Müller's original implementation, and the de facto
 * minimum tape size for a "proper" implementation, is 30,000. I increased that
 * to the nearest power of 2 (namely 32768), because I can, and it's cleaner. */
#define TAPE_SIZE 32768

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

/* virtual address of the section containing the machine code
 * chosen to avoid overlapping with the tape. */
#define LOAD_VADDR 0x20000

/* physical address of the starting instruction */
#define START_PADDR padTo256((EHDR_SIZE + PHTB_SIZE))

/* virtual address of the starting instruction */
#define START_VADDR (START_PADDR + LOAD_VADDR)

/* The offset within the file for the program and section header tables
 * respectively. If there are no entries, they should be set to 0. */
/* program header table is right after the ELF header. */
#define PHOFF (PHNUM ? EHDR_SIZE : 0)

/* section header table offset is 0, as there is no section header table. */
#define SHOFF 0

/* codesize must be defined as the size in bytes of the machine code.
 * FILE_SIZE should not be used until after its final value is known.
 * It is the size (in bytes) of the file. */
#define FILE_SIZE (START_PADDR + codesize)

/* the memory address of the current instruction. */
#define CURRENT_ADDRESS (START_PADDR + codesize)

/* Linux system call numbers */

#define SYSCALL_READ 0
#define SYSCALL_WRITE 1
#define SYSCALL_EXIT 60

/* Miscellaneous */

/* common elements to all functions to compile specific instructions
 * each function must do the following:
 * 1: define a uint8_t array called instructionBytes containing the machine code
 * 2: write instructionBytes to a file descriptor fd,
 * 3: add the number of bytes written to codesize
 * 4: return a truthy value if the write succeeded, or a falsy value otherwise
 * This macro takes care of steps 2 through 4, meaning that the functions
 * only need to define instructionBytes then call this macro. */
#define writeInstructionBytes() \
    ssize_t written = write(fd, &instructionBytes, sizeof(instructionBytes)); \
    codesize += written; \
    return written == sizeof(instructionBytes) /* exclude trailing semicolon */

#endif /* EAMASM_MACROS */
