/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains functions that encode x86_64 machine instructions and
 * write them to open file descriptors. Each returns a boolean value indicating
 * whether or not the write was successful. */

/* C99 */
#include <stdbool.h>
#include <stdint.h>
/* POSIX */
#include <unistd.h>
/* internal */
#include "serialize.h"

/* MOV rs, rd */
bool eamAsmRegCopy(unsigned char dst, unsigned char src, int fd) {
    return write(fd, (uint8_t[]){ 0x89U, 0xc0U + (src << 3) + dst}, 2) == 2;
}

/* SYSCALL */
bool eamAsmSyscall(int fd) {
    return write(fd, (uint8_t[]){ 0x0f, 0x05 }, 2) == 2;
}

/* TEST byte [reg], 0xff */
static inline \
    bool eamAsmJumpTest(unsigned char reg, int fd) {
    return write(fd, (uint8_t[]){ 0xf6, reg, 0xff}, 3) == 3;
}

/* TEST byte [reg], 0xff; Jcc|tttn jmp_offset */
static inline \
    bool eamAsmCondJump(char tttn, unsigned char reg, int32_t offset, int fd) {
    uint8_t i_bytes[6] = { 0x0f, 0x80|tttn, 0x00, 0x00, 0x00, 0x00 };
    /* need to test the byte first */
    if (!eamAsmJumpTest(reg, fd)) return false;
    /* need to cast to uint32_t for serialize32.
     * Acceptable as POSIX requires 2's complement for signed types. */
    if (serialize32((uint32_t)offset, (char *)&i_bytes[2]) != 4) return false;
    return write(fd, &i_bytes, 6) == 6;
}

/* TEST byte [reg], 0xff; JNZ jmp_offset */
bool eamAsmJumpNotZero(unsigned char reg, int32_t offset, int fd) {
    /* Jcc with tttn=0b0101 is JNZ or JNE */
    return eamAsmCondJump(0x5, reg, offset, fd);
}

/* TEST byte [reg], 0xff; JZ jmp_offset */
bool eamAsmJumpZero(unsigned char reg, int32_t offset, int fd) {
    /* Jcc with tttn=0b0100 is JZ or JE */
    return eamAsmCondJump(0x4, reg, offset, fd);
}

/* INC and DEC are encoded very similarly with very few differences between
 * the encoding for operating on registers and operating on bytes pointed to by
 * registers. Because of the similarity, one function can be used for all 4 of
 * the `+`, `-`, `>`, and `<` brainfuck instructions in one inline function.
 *
 * `+` is INC byte [reg], which is encoded as 0xfe reg
 * `-` is DEC byte [reg], which is encoded as 0xfe 0x08|reg
 * `>` is INC reg, which is encoded as 0xff 0xc0|reg
 * `<` is DEC reg, which is encoded as 0xff 0xc8|reg
 *
 * Therefore, setting op to 0 for INC and 8 for DEC and adm (ADddress Mode) to 3
 * when working on registers and 0 when working on memory, then doing some messy
 * bitwise hackery, the following function can be used. */
static inline \
    bool eamAsmOffset(char op, unsigned char adm, unsigned char reg, int fd) {
    return write(fd, (uint8_t[]){ 0xfe | (adm&1), (op|reg|(adm<<6)) }, 2) == 2;
}

/* INC reg */
bool eamAsmIncReg(unsigned char reg, int fd) {
    /* 0 is INC, 3 is register mode */
    return eamAsmOffset(0x0, 0x3, reg, fd);
}

/* DEC reg */
bool eamAsmDecReg(unsigned char reg, int fd) {
    /* 8 is DEC, 3 is register mode */
    return eamAsmOffset(0x8, 0x3, reg, fd);
}

/* INC byte [reg] */
bool eamAsmIncByte(unsigned char reg, int fd) {
    /* 0 is INC, 0 is memory pointer mode */
    return eamAsmOffset(0x0, 0x0, reg, fd);
}

/* DEC byte [reg] */
bool eamAsmDecByte(unsigned char reg, int fd) {
    /* 8 is DEC, 0 is memory pointer mode */
    return eamAsmOffset(0x8, 0x0, reg, fd);
}

/* MOV reg, imm32 */
static inline \
    bool eamAsmSetRegDoubleWord(unsigned char reg, int32_t imm32, int fd) {
    uint8_t i_bytes[5] = { 0xb8 + reg, 0x00, 0x00, 0x00, 0x00 };
    /* need to cast to uint32_t for serialize32.
     * Acceptable as POSIX requires 2's complement for signed types. */
    if (serialize32((uint32_t)imm32, (char *)&i_bytes[1]) != 4) return false;
    return write(fd, &i_bytes, 5) == 5;
}

/* PUSH imm8; POP reg */
static inline \
bool eamAsmSetRegByte(unsigned char reg, int8_t imm8, int fd) {
    return write(fd, (uint8_t[]){ 0x6a, (uint8_t)imm8, 0x58 + reg}, 3) == 3;
}

/* more efficient than PUSH 0; POP reg */
/* XOR reg, reg */
static inline \
    bool eamAsmSetRegZero(unsigned char reg, int fd) {
    return write(fd, (uint8_t[]){ 0x31, 0xc0|(reg<<3)|reg }, 2) == 2;
}

/* use the most efficient way to set a register to imm */
bool eamAsmSetReg(unsigned char reg, int32_t imm, int fd) {
    if (imm == 0) return eamAsmSetRegZero(reg, fd);
    
    if (imm >= INT8_MIN && imm <= INT8_MAX) {
        return eamAsmSetRegByte(reg, (int8_t)imm, fd);
    }

    /* fall back to using the full 32-bit value */
    return eamAsmSetRegDoubleWord(reg, imm, fd);
}

/* TODO - machine code for extra instructions from eambfc-ir */
/* } */
bool eamAsmAddRegByte(unsigned char reg, int8_t imm8, int fd);
/* { */
bool eamAsmSubRegByte(unsigned char reg, int8_t imm8, int fd);
/* ) */
bool eamAsmAddRegWord(unsigned char reg, int16_t imm16, int fd);
/* ( */
bool eamAsmSubRegWord(unsigned char reg, int16_t imm16, int fd);
/* $ */
bool eamAsmAddRegDoubleWord(unsigned char reg, int32_t imm32, int fd);
/* ^ */
bool eamAsmSubRegDoubleWord(unsigned char reg, int32_t imm32, int fd);
/* n */
bool eamAsmAddRegQuadWord(unsigned char reg, int64_t imm64, int fd);
/* N */
bool eamAsmSubRegQuadWord(unsigned char reg, int64_t imm64, int fd);
/* # */
bool eamAsmAddMem(unsigned char rec, int8_t imm8, int fd);
/* = */
bool eamAsmSubMem(unsigned char rec, int8_t imm8, int fd);
/* @ */
bool eamAsmSetMemZero(unsigned char reg, int fd);
