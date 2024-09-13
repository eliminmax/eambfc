/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file provides the arch_inter for the x86_64 architecture. */

/* POSIX */
#include <unistd.h> /* write */
/* internal */
#include "arch_inter.h" /* arch_{registers, sc_nums, funcs, inter} */
#include "compat/elf.h" /* EM_X86_64, ELFDATA2LSB */
#include "err.h" /* basic_err */
#include "serialize.h" /* serialize* */
#include "types.h" /* uint*_t, int*_t, bool, size_t, off_t */
#include "util.h" /* write_obj */

/* in eambfc, `[` and `]` are both compiled to TEST (3 bytes), followed by a Jcc
 * instruction (6 bytes). When encountering a `[` instruction, skip this many
 * bytes to leave room for them. */
#define JUMP_SIZE 9


/* if there are more than 3 lines in common between similar ADD/SUB or JZ/JNZ
 * x86_64_ functions, the common lines dealing with writing machine code should
 * be moved into a static inline function. */

/* MOV rs, rd */
bool x86_64_reg_copy(uint8_t dst, uint8_t src, int fd, off_t *sz) {
    return write_obj(fd, (uint8_t[]){ 0x89, 0xc0 + (src << 3) + dst}, 2, sz);
}

/* SYSCALL */
bool x86_64_syscall(int fd, off_t *sz) {
    return write_obj(fd, (uint8_t[]){ 0x0f, 0x05 }, 2, sz);
}

/* TEST byte [reg], 0xff; Jcc|tttn off */
static bool test_jcc(
    char tttn, uint8_t reg, int32_t off, int fd, off_t *sz
) {
    uint8_t i_bytes[9] = {
        /* TEST byte [reg], 0xff */
        0xf6, reg, 0xff,
        /* Jcc|tttn 0x00000000 (will replace with jump offset) */
        0x0f, 0x80 | tttn, 0x00, 0x00, 0x00, 0x00
    };
    /* need to cast to uint32_t for serialize32.
     * Acceptable as POSIX requires 2's complement for signed types. */
    if (serialize32(off, (char *)&i_bytes[5]) != 4) return false;
    return write_obj(fd, &i_bytes, 9, sz);
}

/* TEST byte [reg], 0xff; JNZ jmp_offset */
bool x86_64_jump_not_zero(uint8_t reg, int64_t offset, int fd, off_t *sz) {
    if (offset > INT32_MAX || offset < INT32_MIN) {
        basic_err(
            "JUMP_TOO_LONG",
            "offset is outside the range of possible 32-bit signed values"
        );
        return false;
    }
    /* Jcc with tttn=0b0101 is JNZ or JNE */
    return test_jcc(0x5, reg, offset, fd, sz);
}

/* TEST byte [reg], 0xff; JZ jmp_offset */
bool x86_64_jump_zero(uint8_t reg, int64_t offset, int fd, off_t *sz) {
    if (offset > INT32_MAX || offset < INT32_MIN) {
        basic_err(
            "JUMP_TOO_LONG",
            "offset is outside the range of possible 32-bit signed values"
        );
        return false;
    }
    /* Jcc with tttn=0b0100 is JZ or JE */
    return test_jcc(0x4, reg, offset, fd, sz);
}

/* times JUMP_SIZE NOP */
bool x86_64_nop_loop_open(int fd, off_t *sz) {
    uint8_t nops[JUMP_SIZE];
    for (int i = 0; i < JUMP_SIZE; i++) nops[i] = 0x90;
    return write_obj(fd, &nops, JUMP_SIZE, sz);
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
static inline bool x86_offset(
    char op, uint8_t adm, uint8_t reg, int fd, off_t *sz
) {
    return write_obj(fd, (uint8_t[]){0xfe | (adm&1), (op|reg|(adm<<6))}, 2, sz);
}

/* INC reg */
bool x86_64_inc_reg(uint8_t reg, int fd, off_t *sz) {
    /* 0 is INC, 3 is register mode */
    return x86_offset(0x0, 0x3, reg, fd, sz);
}

/* DEC reg */
bool x86_64_dec_reg(uint8_t reg, int fd, off_t *sz) {
    /* 8 is DEC, 3 is register mode */
    return x86_offset(0x8, 0x3, reg, fd, sz);
}

/* INC byte [reg] */
bool x86_64_inc_byte(uint8_t reg, int fd, off_t *sz) {
    /* 0 is INC, 0 is memory pointer mode */
    return x86_offset(0x0, 0x0, reg, fd, sz);
}

/* DEC byte [reg] */
bool x86_64_dec_byte(uint8_t reg, int fd, off_t *sz) {
    /* 8 is DEC, 0 is memory pointer mode */
    return x86_offset(0x8, 0x0, reg, fd, sz);
}

/* use the most efficient way to set a register to imm */
bool x86_64_set_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    if (imm == 0) {
        /* XOR reg, reg */
        return write_obj(fd, (uint8_t[]){ 0x31, 0xc0|(reg<<3)|reg }, 2, sz);
    } else if (imm >= INT8_MIN && imm <= INT8_MAX) {
        /* PUSH imm8; POP reg */
        return write_obj(fd, (uint8_t[]){ 0x6a, imm, 0x58 + reg}, 3, sz);
    } else if (imm >= INT32_MIN && imm <= INT32_MAX) {
        /* MOV reg, imm32 */
        uint8_t instr_bytes[5] = { 0xb8 | reg, 0x00, 0x00, 0x00, 0x00 };
        if (serialize32(imm32, (char *)&(instr_bytes[1])) != 4) return false;
        return write_obj(fd, &instr_bytes, 5, sz);
    } else {
        /* MOV reg, imm64 */
        uint8_t instr_bytes[10] = {
            0x48, 0xb8 | reg, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        if (serialize64(imm64, (char *)&(instr_bytes[2])) != 8) return false;
        return write_obj(fd, &instr_bytes, 10, sz);
    }
}

/* } */
bool x86_64_add_reg_imm8(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    return write_obj(fd, (uint8_t[]){ 0x83, 0xc0 + reg, imm8 }, 3, sz);
}

/* { */
bool x86_64_sub_reg_imm8(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    return write_obj(fd, (uint8_t[]){ 0x83, 0xe8 + reg, imm8 }, 3, sz);
}

/* ) */
bool x86_64_add_reg_imm16(uint8_t reg, int16_t imm16, int fd, off_t *sz) {
    /* use the 32-bit immediate instruction, as there's no 16-bit equivalent. */
    uint8_t i_bytes[6] = { 0x81, 0xc0 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize16(imm16, (char *)&i_bytes[2]) != 2) return false;
    return write_obj(fd, &i_bytes, 6, sz);
}

/* ( */
bool x86_64_sub_reg_imm16(uint8_t reg, int16_t imm16, int fd, off_t *sz) {
    /* use the 32-bit immediate instruction, as there's no 16-bit equivalent. */
    uint8_t i_bytes[6] = { 0x81, 0xe8 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize16(imm16, (char *)&i_bytes[2]) != 2) return false;
    return write_obj(fd, &i_bytes, 6, sz);
}

/* $ */
bool x86_64_add_reg_imm32(uint8_t reg, int32_t imm32, int fd, off_t *sz) {
    uint8_t i_bytes[6] = { 0x81, 0xc0 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize32(imm32, (char *)&i_bytes[2]) != 4) return false;
    return write_obj(fd, &i_bytes, 6, sz);
}

/* ^ */
bool x86_64_sub_reg_imm32(uint8_t reg, int32_t imm32, int fd, off_t *sz) {
    uint8_t i_bytes[6] = { 0x81, 0xe8 + reg, 0x00, 0x00, 0x00, 0x00 };
    if (serialize32(imm32, (char *)&i_bytes[2]) != 4) return false;
    return write_obj(fd, &i_bytes, 6, sz);
}

static inline bool add_sub_imm64(
    uint8_t reg, int64_t imm64, int fd, uint8_t op, off_t *sz
) {
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
    return write_obj(fd, &i_bytes, 15, sz);

}

/* n */
bool x86_64_add_reg_imm64(uint8_t reg, int64_t imm64, int fd, off_t *sz) {
    /* 0x00 is the opcode for register-to-register ADD */
    return add_sub_imm64(reg, imm64, fd, 0x00, sz);
}

/* N */
bool x86_64_sub_reg_imm64(
    uint8_t reg, int64_t imm64, int fd, off_t *sz) {
    /* 0x28 is the opcode for register-to-register SUB */
    return add_sub_imm64(reg, imm64, fd, 0x28, sz);
}

bool x86_64_add_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    if (imm == 0) {
        return true; /* adding zero is a no-op */
    } else if (imm >= INT8_MIN && imm < INT8_MAX) {
        return x86_64_add_reg_imm8(reg, imm, fd, sz);
    } else if (imm >= INT32_MIN && imm < INT32_MAX) {
        return x86_64_add_reg_imm32(reg, imm, fd, sz);
    } else {
        return x86_64_add_reg_imm64(reg, imm, fd, sz);
    }
}


bool x86_64_sub_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    if (imm == 0) {
        return true; /* subtracting zero is a no-op */
    } else if (imm >= INT8_MIN && imm < INT8_MAX) {
        return x86_64_sub_reg_imm8(reg, imm, fd, sz);
    } else if (imm >= INT32_MIN && imm < INT32_MAX) {
        return x86_64_sub_reg_imm32(reg, imm, fd, sz);
    } else {
        return x86_64_sub_reg_imm64(reg, imm, fd, sz);
    }
}

/* # */
bool x86_64_add_byte(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    /* ADD byte [reg], imm8 */
    return write_obj(fd, (uint8_t[]){ 0x80, reg, imm8}, 3, sz);
}

/* = */
bool x86_64_sub_byte(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    /* SUB byte [reg], imm8 */
    return write_obj(fd, (uint8_t[]){ 0x80, 0x28 + reg, imm8}, 3, sz);
}

/* @ */
bool x86_64_zero_byte(uint8_t reg, int fd, off_t *sz) {
    /* MOV byte [reg], 0 */
    return write_obj(fd, (uint8_t[]){ 0x67, 0xc6, reg, 0x00 }, 4, sz);
}

const arch_funcs X86_64_FUNCS = {
    x86_64_set_reg,
    x86_64_reg_copy,
    x86_64_syscall,
    x86_64_nop_loop_open,
    x86_64_jump_zero,
    x86_64_jump_not_zero,
    x86_64_inc_reg,
    x86_64_dec_reg,
    x86_64_inc_byte,
    x86_64_dec_byte,
    x86_64_add_reg,
    x86_64_sub_reg,
    x86_64_add_byte,
    x86_64_sub_byte,
    x86_64_zero_byte
};

const arch_sc_nums X86_64_SC_NUMS = {
    0 /* read */,
    1 /* write */,
    60 /* exit */
};

const arch_registers X86_64_REGS = {
    00 /* sc_num = RAX */,
    07 /* arg1 = RDI */,
    06 /* arg2 = RSI */,
    02 /* arg3 = RDX */,
    03 /* bf_ptr = RBX */
};

const arch_inter X86_64_INTER = {
    &X86_64_FUNCS,
    &X86_64_SC_NUMS,
    &X86_64_REGS,
    0 /* no flags are defined for this architecture */,
    EM_X86_64,
    ELFDATA2LSB
};
