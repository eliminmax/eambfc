/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains functions that encode x86_64 machine instructions and
 * write them to open file descriptors. Each returns a boolean value indicating
 * whether or not the write was successful. */

/* POSIX */
#include <unistd.h> /* size_t, off_t, write */
/* internal */
#include "serialize.h" /* serialize* */
#include "types.h" /* uint*_t, int*_t, bool */
#include "x86_64_constants.h" /* JUMP_SIZE */

/* if there are more than 3 lines in common between similar ADD/SUB or JZ/JNZ
 * bfc_ functions, the common lines dealing with writing machine code should
 * be moved into a static inline function. */

/* MOV rs, rd */
bool bfc_reg_copy(uint8_t dst, uint8_t src, int fd, off_t *sz) {
    *sz += 2;
    return write(fd, (uint8_t[]){ 0x89, 0xc0 + (src << 3) + dst}, 2) == 2;
}

/* SYSCALL */
bool bfc_syscall(int fd, off_t *sz) {
    *sz += 2;
    return write(fd, (uint8_t[]){ 0x0f, 0x05 }, 2) == 2;
}

/* TEST byte [reg], 0xff; Jcc|tttn off */
static inline bool test_jcc(char tttn, uint8_t reg, int32_t off, int fd) {
    uint8_t i_bytes[9] = {
        /* TEST byte [reg], 0xff */
        0xf6, reg, 0xff,
        /* Jcc|tttn 0x00000000 (will replace with jump offset) */
        0x0f, 0x80 | tttn, 0x00, 0x00, 0x00, 0x00
    };
    /* need to cast to uint32_t for serialize32.
     * Acceptable as POSIX requires 2's complement for signed types. */
    if (serialize32(off, (char *)&i_bytes[5]) != 4) return false;
    return write(fd, &i_bytes, 9) == 9;
}

/* TEST byte [reg], 0xff; JNZ jmp_offset */
bool bfc_jump_not_zero(uint8_t reg, int32_t offset, int fd, off_t *sz) {
    *sz += 9;
    /* Jcc with tttn=0b0101 is JNZ or JNE */
    return test_jcc(0x5, reg, offset, fd);
}

/* TEST byte [reg], 0xff; JZ jmp_offset */
bool bfc_jump_zero(uint8_t reg, int32_t offset, int fd, off_t *sz) {
    *sz += 9;
    /* Jcc with tttn=0b0100 is JZ or JE */
    return test_jcc(0x4, reg, offset, fd);
}

/* times JUMP_SIZE NOP */
bool bfc_nop_loop_open(int fd, off_t *sz) {
    *sz += 9;
    uint8_t nops[JUMP_SIZE];
    for (int i = 0; i < JUMP_SIZE; i++) nops[i] = 0x90;

    return write(fd, &nops, JUMP_SIZE) == JUMP_SIZE;
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
 * Therefore, setting op to 0 for INC and 8 for DEC and adm (Address Mode) to 3
 * when working on registers and 0 when working on memory, then doing some messy
 * bitwise hackery, the following function can be used. */
static inline bool x86_offset(char op, uint8_t adm, uint8_t reg, int fd) {
    return write(fd, (uint8_t[]){ 0xfe | (adm&1), (op|reg|(adm<<6)) }, 2) == 2;
}

/* INC reg */
bool bfc_inc_reg(uint8_t reg, int fd, off_t *sz) {
    *sz += 2;
    /* 0 is INC, 3 is register mode */
    return x86_offset(0x0, 0x3, reg, fd);
}

/* DEC reg */
bool bfc_dec_reg(uint8_t reg, int fd, off_t *sz) {
    *sz += 2;
    /* 8 is DEC, 3 is register mode */
    return x86_offset(0x8, 0x3, reg, fd);
}

/* INC byte [reg] */
bool bfc_inc_byte(uint8_t reg, int fd, off_t *sz) {
    *sz += 2;
    /* 0 is INC, 0 is memory pointer mode */
    return x86_offset(0x0, 0x0, reg, fd);
}

/* DEC byte [reg] */
bool bfc_dec_byte(uint8_t reg, int fd, off_t *sz) {
    *sz += 2;
    /* 8 is DEC, 0 is memory pointer mode */
    return x86_offset(0x8, 0x0, reg, fd);
}

/* MOV reg, imm32 */
static inline bool set_reg_dword(uint8_t reg, int32_t imm32, int fd) {
    uint8_t i_bytes[5] = { 0xb8 + reg, 0x00, 0x00, 0x00, 0x00 };
    /* need to cast to uint32_t for serialize32.
     * Acceptable as POSIX requires 2's complement for signed types. */
    if (serialize32(imm32, (char *)&i_bytes[1]) != 4) return false;
    return write(fd, &i_bytes, 5) == 5;
}

/* use the most efficient way to set a register to imm */
bool bfc_set_reg(uint8_t reg, int32_t imm, int fd, off_t *sz) {
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
    return set_reg_dword(reg, imm, fd);
}

/* } */
bool bfc_add_reg_imm8(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    *sz += 3;
    return write(fd, (uint8_t[]){ 0x83, 0xc0 + reg, imm8 }, 3) == 3;
}

/* { */
bool bfc_sub_reg_imm8(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    *sz += 3;
    return write(fd, (uint8_t[]){ 0x83, 0xe8 + reg, imm8 }, 3) == 3;
}

/* ) */
bool bfc_add_reg_imm16(uint8_t reg, int16_t imm16, int fd, off_t *sz) {
    /* use the 32-bit immediate instruction, as there's no 16-bit equivalent. */
    *sz += 6;
    uint8_t i_bytes[6] = { 0x81, 0xc0 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize16(imm16, (char *)&i_bytes[2]) != 2) return false;
    return write(fd, &i_bytes, 6) == 6;
}

/* ( */
bool bfc_sub_reg_imm16(uint8_t reg, int16_t imm16, int fd, off_t *sz) {
    /* use the 32-bit immediate instruction, as there's no 16-bit equivalent. */
    *sz += 6;
    uint8_t i_bytes[6] = { 0x81, 0xe8 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize16(imm16, (char *)&i_bytes[2]) != 2) return false;
    return write(fd, &i_bytes, 6) == 6;
}

/* $ */
bool bfc_add_reg_imm32(uint8_t reg, int32_t imm32, int fd, off_t *sz) {
    *sz += 6;
    uint8_t i_bytes[6] = { 0x81, 0xc0 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize32(imm32, (char *)&i_bytes[2]) != 4) return false;
    return write(fd, &i_bytes, 6) == 6;
}

/* ^ */
bool bfc_sub_reg_imm32(uint8_t reg, int32_t imm32, int fd, off_t *sz) {
    *sz += 6;
    uint8_t i_bytes[6] = { 0x81, 0xe8 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize32(imm32, (char *)&i_bytes[2]) != 4) return false;
    return write(fd, &i_bytes, 6) == 6;
}

static inline bool add_sub_qw(uint8_t reg, int64_t imm64, int fd, uint8_t op) {
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
bool bfc_add_reg_imm64(uint8_t reg, int64_t imm64, int fd, off_t *sz) {
    *sz += 15;
    /* 0x00 is the opcode for register-to-register ADD */
    return add_sub_qw(reg, imm64, fd, 0x00);
}

/* N */
bool bfc_sub_reg_imm64(uint8_t reg, int64_t imm64, int fd, off_t *sz) {
    *sz += 15;
    /* 0x28 is the opcode for register-to-register SUB */
    return add_sub_qw(reg, imm64, fd, 0x28);
}

bool bfc_add_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    if (imm == 0) {
        return true; /* adding zero is a no-op */
    } else if (imm >= INT8_MIN && imm < INT8_MAX) {
        return bfc_add_reg_imm8(reg, imm, fd, sz);
    } else if (imm >= INT32_MIN && imm < INT32_MAX) {
        return bfc_add_reg_imm32(reg, imm, fd, sz);
    } else {
        return bfc_add_reg_imm64(reg, imm, fd, sz);
    }
}


bool bfc_sub_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    if (imm == 0) {
        return true; /* subtracting zero is a no-op */
    } else if (imm >= INT8_MIN && imm < INT8_MAX) {
        return bfc_sub_reg_imm8(reg, imm, fd, sz);
    } else if (imm >= INT32_MIN && imm < INT32_MAX) {
        return bfc_sub_reg_imm32(reg, imm, fd, sz);
    } else {
        return bfc_sub_reg_imm64(reg, imm, fd, sz);
    }
}

/* # */
bool bfc_add_mem(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    *sz += 3;
    /* ADD byte [reg], imm8 */
    return write(fd, (uint8_t[]){ 0x80, reg, imm8}, 3) == 3;
}

/* = */
bool bfc_sub_mem(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    *sz += 3;
    /* SUB byte [reg], imm8 */
    return write(fd, (uint8_t[]){ 0x80, 0x28 + reg, imm8}, 3) == 3;
}

/* @ */
bool bfc_zero_mem(uint8_t reg, int fd, off_t *sz) {
    *sz += 4;
    /* MOV byte [reg], 0 */
    return write(fd, (uint8_t[]){ 0x67, 0xc6, reg, 0x00 }, 4) == 4;
}
