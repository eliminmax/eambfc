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

/* if there are more than 3 lines in common between equivalent ADD/SUB or JZ/JNZ
 * eamAsm functions, the common lines dealing with writing machine code should
 * be moved into a static inline function. */

/* MOV rs, rd */
bool eamAsmRegCopy(uint8_t dst, uint8_t src, int fd, off_t *sz) {
    *sz += 2;
    return write(fd, (uint8_t[]){ 0x89, 0xc0 + (src << 3) + dst}, 2) == 2;
}

/* SYSCALL */
bool eamAsmSyscall(int fd, off_t *sz) {
    *sz += 2;
    return write(fd, (uint8_t[]){ 0x0f, 0x05 }, 2) == 2;
}

/* TEST byte [reg], 0xff */
static inline bool eamAsmJumpTest(uint8_t reg, int fd) {
    return write(fd, (uint8_t[]){ 0xf6, reg, 0xff}, 3) == 3;
}

/* TEST byte [reg], 0xff; Jcc|tttn jmp_offset */
static inline bool eamAsmJcc(char tttn, uint8_t reg, int32_t offset, int fd) {
    uint8_t i_bytes[6] = { 0x0f, 0x80|tttn, 0x00, 0x00, 0x00, 0x00 };
    /* need to test the byte first */
    if (!eamAsmJumpTest(reg, fd)) return false;
    /* need to cast to uint32_t for serialize32.
     * Acceptable as POSIX requires 2's complement for signed types. */
    if (serialize32(offset, (char *)&i_bytes[2]) != 4) return false;
    return write(fd, &i_bytes, 6) == 6;
}

/* TEST byte [reg], 0xff; JNZ jmp_offset */
bool eamAsmJumpNotZero(uint8_t reg, int32_t offset, int fd, off_t *sz) {
    *sz += 9;
    /* Jcc with tttn=0b0101 is JNZ or JNE */
    return eamAsmCondJump(0x5, reg, offset, fd);
}

/* TEST byte [reg], 0xff; JZ jmp_offset */
bool eamAsmJumpZero(uint8_t reg, int32_t offset, int fd, off_t *sz) {
    *sz += 9;
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
 * Therefore, setting op to 0 for INC and 8 for DEC and adm (Adddress Mode) to 3
 * when working on registers and 0 when working on memory, then doing some messy
 * bitwise hackery, the following function can be used. */
static inline bool x86Offset(char op, uint8_t adm, uint8_t reg, int fd) {
    return write(fd, (uint8_t[]){ 0xfe | (adm&1), (op|reg|(adm<<6)) }, 2) == 2;
}

/* INC reg */
bool eamAsmIncReg(uint8_t reg, int fd, off_t *sz) {
    *sz += 2;
    /* 0 is INC, 3 is register mode */
    return x86Offset(0x0, 0x3, reg, fd);
}

/* DEC reg */
bool eamAsmDecReg(uint8_t reg, int fd, off_t *sz) {
    *sz += 2;
    /* 8 is DEC, 3 is register mode */
    return x86Offset(0x8, 0x3, reg, fd);
}

/* INC byte [reg] */
bool eamAsmIncByte(uint8_t reg, int fd, off_t *sz) {
    *sz += 2;
    /* 0 is INC, 0 is memory pointer mode */
    return x86Offset(0x0, 0x0, reg, fd);
}

/* DEC byte [reg] */
bool eamAsmDecByte(uint8_t reg, int fd, off_t *sz) {
    *sz += 2;
    /* 8 is DEC, 0 is memory pointer mode */
    return x86Offset(0x8, 0x0, reg, fd);
}

/* MOV reg, imm32 */
static inline bool x86SetRegDoubleWord(uint8_t reg, int32_t imm32, int fd) {
    uint8_t i_bytes[5] = { 0xb8 + reg, 0x00, 0x00, 0x00, 0x00 };
    /* need to cast to uint32_t for serialize32.
     * Acceptable as POSIX requires 2's complement for signed types. */
    if (serialize32(imm32, (char *)&i_bytes[1]) != 4) return false;
    return write(fd, &i_bytes, 5) == 5;
}

/* use the most efficient way to set a register to imm */
bool eamAsmSetReg(uint8_t reg, int32_t imm, int fd, off_t *sz) {
    if (imm == 0) {
        *sz += 2;
        /* XOR reg, reg */
        return write(fd, (uint8_t[]){ 0x31, 0xc0|(reg<<3)|reg }, 2) == 2;
    }

    if (imm >= INT8_MIN && imm <= INT8_MAX) {
        *sz += 3;
        /* PUSH imm8; POP reg */
        return write(fd, (uint8_t[]){ 0x6a, imm, 0x58 + reg}, 3) == 3;
    }

    /* fall back to using the full 32-bit value */
    *sz += 5;
    return eamAsmSetRegDoubleWord(reg, imm, fd);
}

/* } */
bool eamAsmAddRegByte(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    *sz += 3;
    return write(fd, (uint8_t[]){ 0x83, 0xc0 + reg, imm8 }, 3) == 3;
}

/* { */
bool eamAsmSubRegByte(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    *sz += 3;
    return write(fd, (uint8_t[]){ 0x83, 0xe8 + reg, imm8 }, 3) == 3;
}

/* ) */
bool eamAsmAddRegWord(uint8_t reg, int16_t imm16, int fd, off_t *sz) {
    /* use the 32-bit immediate instruction, as there's no 16-bit equivalent. */
    *sz += 6;
    uint8_t i_bytes[6] = { 0x81, 0xc0 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize16(imm16, (char *)&i_bytes[2]) != 2) return false;
    return write(fd, &i_bytes, 6) == 6;
}

/* ( */
bool eamAsmSubRegWord(uint8_t reg, int16_t imm16, int fd, off_t *sz) {
    /* use the 32-bit immediate instruction, as there's no 16-bit equivalent. */
    *sz += 6;
    uint8_t i_bytes[6] = { 0x81, 0xe8 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize16(imm16, (char *)&i_bytes[2]) != 2) return false;
    return write(fd, &i_bytes, 6) == 6;
}

/* $ */
bool eamAsmAddRegDoubWord(uint8_t reg, int32_t imm32, int fd, off_t *sz) {
    *sz += 6;
    uint8_t i_bytes[6] = { 0x81, 0xc0 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize32(imm32, (char *)&i_bytes[2]) != 4) return false;
    return write(fd, &i_bytes, 6) == 6;
}

/* ^ */
bool eamAsmSubRegDoubWord(uint8_t reg, int32_t imm32, int fd, off_t *sz) {
    *sz += 6;
    uint8_t i_bytes[6] = { 0x81, 0xe8 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize32(imm32, (char *)&i_bytes[2]) != 4) return false;
    return write(fd, &i_bytes, 6) == 6;
}

static inline bool x86AddSubQW(uint8_t reg, int64_t imm64, int fd, uint8_t op) {
    /* There are no instructions to add or subtract a 64-bit immediate. Instead,
     * the approach  to use is first PUSH the value of a different register, MOV
     * the 64-bit immediate to that register, ADD/SUB that register to the
     * target register, then POP that temporary register, to restore its
     * original value. */
    /* the temporary register shouldn't be the target register */
    uint8_t tmp_reg = reg ? 0 : 1;
    uint8_t i_bytes[] = {
        /* PUSH tmp_reg */
        0x50 + tmp_reg,
        /* MOV tmp_reg, 0x0000000000000000 (will replace with imm64) */
        0x48, 0xb8 + tmp_reg, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* (ADD||SUB) reg, tmp_reg */
        0x48, 0x01 + op, 0xc0 + (tmp_reg << 3) + reg,
        /* POP tmp_reg */
        0x58 + tmp_reg
    };
    /* replace 0x0000000000000000 with imm64 */
    if (serialize64(imm64, (char *)&i_bytes[3]) != 8) return false;
    return write(fd, &i_bytes, 15) == 15;

}

/* n */
bool eamAsmAddRegQuadWord(uint8_t reg, int64_t imm64, int fd, off_t *sz) {
    *sz += 15;
    /* 0x00 is the opcode for register-to-register ADD */
    return x86AddSubQW(reg, imm64, fd, 0x00);
}

/* N */
bool eamAsmSubRegQuadWord(uint8_t reg, int64_t imm64, int fd, off_t *sz) {
    *sz += 15;
    /* 0x28 is the opcode for register-to-register SUB */
    return x86AddSubQW(reg, imm64, fd, 0x28);
}

/* # */
bool eamAsmAddMem(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    *sz += 3;
    /* ADD byte [reg], imm8 */
    return write(fd, (uint8_t[]){ 0x80, reg, imm8}, 3) == 3;
}

/* = */
bool eamAsmSubMem(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    *sz += 3;
    /* SUB byte [reg], imm8 */
    return write(fd, (uint8_t[]){ 0x80, 0x28 + reg, imm8}, 3) == 3;
}

/* @ */
bool eamAsmSetMemZero(uint8_t reg, int fd, off_t *sz) {
    *sz += 4;
    /* MOV byte [reg], 0 */
    return write(fd, (uint8_t[]){ 0x67, 0xc6, reg, 0x00 }, 4) == 4;
}
