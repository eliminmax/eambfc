/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file provides the arch_inter for the x86_64 architecture. */
/* internal */
#include "arch_inter.h" /* arch_{registers, sc_nums, funcs, inter} */
#include "compat/elf.h" /* EM_X86_64, ELFDATA2LSB */
#include "err.h" /* basic_err */
#include "serialize.h" /* serialize* */
#include "types.h" /* [iu]{8,16,32,64}, bool, size_t, off_t */
#include "util.h" /* append_obj */
#if EAMBFC_TARGET_X86_64

/* If there are more than 3 lines in common between similar ADD/SUB or JZ/JNZ
 * functions, the common lines dealing with writing machine code should
 * be moved into a separate function. */

/* mark a series of bytes within a u8 array as being a single instruction,
 * mostly to prevent automated code formatting from splitting them up */
#define INSTRUCTION(...) __VA_ARGS__

/* nicer looking than having a bunch of integer literals inline to create the
 * needed space. */
#define IMM32_PADDING 0x00, 0x00, 0x00, 0x00
#define IMM64_PADDING 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

/* most common values for opcodes in add/sub instructions */
typedef enum { X64_OP_ADD = 0xc0, X64_OP_SUB = 0xe8 } arith_op;

/* TEST byte [reg], 0xff; Jcc|tttn offset */
static bool test_jcc(char tttn, u8 reg, i64 offset, sized_buf *dst_buf) {
    if (offset > INT32_MAX || offset < INT32_MIN) {
        basic_err(
            "JUMP_TOO_LONG",
            "offset is outside the range of possible 32-bit signed values"
        );
        return false;
    }
    u8 i_bytes[9] = {
        /* TEST byte [reg], 0xff */
        INSTRUCTION(0xf6, reg, 0xff),
        /* Jcc|tttn 0x00000000 (will replace with jump offset) */
        INSTRUCTION(0x0f, 0x80 | tttn, IMM32_PADDING),
    };
    if (serialize32le(offset, &(i_bytes[5])) != 4) return false;
    return append_obj(dst_buf, &i_bytes, 9);
}

static bool reg_arith(u8 reg, i64 imm, arith_op op, sized_buf *dst_buf) {
    if (imm == 0) {
        return true;
    } else if (imm >= INT8_MIN && imm <= INT8_MAX) {
        /* ADD/SUB reg, byte imm */
        return append_obj(dst_buf, (u8[]){0x83, op + reg, imm}, 3);
    } else if (imm >= INT32_MIN && imm <= INT32_MAX) {
        /* ADD/SUB reg, imm */
        u8 i_bytes[6] = {INSTRUCTION(0x81, op + reg, IMM32_PADDING)};
        if (serialize32le(imm, &(i_bytes[2])) != 4) return false;
        return append_obj(dst_buf, &i_bytes, 6);
    } else {
        /* There are no instructions to add or subtract a 64-bit immediate.
         * Instead, the approach  to use is first PUSH the value of a different
         * register, MOV the 64-bit immediate to that register, ADD/SUB that
         * register to the target register, then POP that temporary register, to
         * restore its original value. */
        /* the temporary register shouldn't be the target register */
        u8 tmp_reg = (reg != 0) ? 0 : 1;
        u8 instr_bytes[] = {
            /* PUSH tmp_reg */
            INSTRUCTION(0x50 + tmp_reg),
            /* MOV tmp_reg, 0x0000000000000000 (will replace with imm64) */
            INSTRUCTION(0x48, 0xb8 + tmp_reg, IMM64_PADDING),
            /* (ADD||SUB) reg, tmp_reg */
            INSTRUCTION(0x48, op - 0xbf, 0xc0 + (tmp_reg << 3) + reg),
            /* POP tmp_reg */
            INSTRUCTION(0x58 + tmp_reg),
        };
        /* replace 0x0000000000000000 with imm64 */
        if (serialize64le(imm, &(instr_bytes[3])) != 8) return false;
        return append_obj(dst_buf, &instr_bytes, 15);
    }
}

/* INC and DEC are encoded very similarly with very few differences between
 * the encoding for operating on registers and operating on bytes pointed to by
 * registers. Because of the similarity, one function can be used for all 4 of
 * the `+`, `-`, `>`, and `<` brainfuck instructions in one function.
 *
 * `+` is INC byte [reg], which is encoded as 0xfe reg
 * `-` is DEC byte [reg], which is encoded as 0xfe 0x08|reg
 * `>` is INC reg, which is encoded as 0xff 0xc0|reg
 * `<` is DEC reg, which is encoded as 0xff 0xc8|reg
 *
 * Therefore, setting op to 0 for INC and 8 for DEC and adm (Address Mode) to 3
 * when working on registers and 0 when working on memory, then doing some messy
 * bitwise hackery, the following function can be used. */
static bool x86_offset(char op, u8 adm, u8 reg, sized_buf *dst_buf) {
    return append_obj(
        dst_buf,
        (u8[]){INSTRUCTION(0xfe | (adm & 1), (op | reg | (adm << 6)))},
        2
    );
}

/* now, the functions exposed through X86_64_INTER */
/* use the most efficient way to set a register to imm */
static bool set_reg(u8 reg, i64 imm, sized_buf *dst_buf) {
    if (imm == 0) {
        /* XOR reg, reg */
        return append_obj(
            dst_buf, (u8[]){INSTRUCTION(0x31, 0xc0 | (reg << 3) | reg)}, 2
        );
    } else if (imm >= INT32_MIN && imm <= INT32_MAX) {
        /* MOV reg, imm32 */
        u8 instr_bytes[5] = {INSTRUCTION(0xb8 | reg, IMM32_PADDING)};
        if (serialize32le(imm, &(instr_bytes[1])) != 4) return false;
        return append_obj(dst_buf, &instr_bytes, 5);
    } else {
        /* MOV reg, imm64 */
        u8 instr_bytes[10] = {INSTRUCTION(0x48, 0xb8 | reg, IMM64_PADDING)};
        if (serialize64le(imm, &(instr_bytes[2])) != 8) return false;
        return append_obj(dst_buf, &instr_bytes, 10);
    }
}

/* MOV rs, rd */
static bool reg_copy(u8 dst, u8 src, sized_buf *dst_buf) {
    return append_obj(
        dst_buf, (u8[]){INSTRUCTION(0x89, 0xc0 + (src << 3) + dst)}, 2
    );
}

/* SYSCALL */
static bool syscall(sized_buf *dst_buf) {
    return append_obj(dst_buf, (u8[]){INSTRUCTION(0x0f, 0x05)}, 2);
}

/* In this backend, `[` and `]` are both compiled to TEST (3 bytes), followed by
 * a Jcc instruction (6 bytes). When encountering a `[` instruction, fill 9
 * * bytes with NOP instructins to leave room for it. */
#define NOP 0x90

/* times 9 NOP */
static bool nop_loop_open(sized_buf *dst_buf) {
    u8 nops[9] = {NOP, NOP, NOP, NOP, NOP, NOP, NOP, NOP, NOP};
    return append_obj(dst_buf, &nops, 9);
}

/* TEST byte [reg], 0xff; JZ jmp_offset */
static bool jump_zero(u8 reg, i64 offset, sized_buf *dst_buf) {
    /* Jcc with tttn=0b0100 is JZ or JE, so use 4 for tttn */
    return test_jcc(0x4, reg, offset, dst_buf);
}

/* TEST byte [reg], 0xff; JNZ jmp_offset */
static bool jump_not_zero(u8 reg, i64 offset, sized_buf *dst_buf) {
    /* Jcc with tttn=0b0101 is JNZ or JNE, so use 5 for tttn */
    return test_jcc(0x5, reg, offset, dst_buf);
}

/* INC reg */
static bool inc_reg(u8 reg, sized_buf *dst_buf) {
    /* 0 is INC, 3 is register mode */
    return x86_offset(0x0, 0x3, reg, dst_buf);
}

/* DEC reg */
static bool dec_reg(u8 reg, sized_buf *dst_buf) {
    /* 8 is DEC, 3 is register mode */
    return x86_offset(0x8, 0x3, reg, dst_buf);
}

/* INC byte [reg] */
static bool inc_byte(u8 reg, sized_buf *dst_buf) {
    /* 0 is INC, 0 is memory pointer mode */
    return x86_offset(0x0, 0x0, reg, dst_buf);
}

/* DEC byte [reg] */
static bool dec_byte(u8 reg, sized_buf *dst_buf) {
    /* 8 is DEC, 0 is memory pointer mode */
    return x86_offset(0x8, 0x0, reg, dst_buf);
}

static bool add_reg(u8 reg, i64 imm, sized_buf *dst_buf) {
    return reg_arith(reg, imm, X64_OP_ADD, dst_buf);
}

static bool sub_reg(u8 reg, i64 imm, sized_buf *dst_buf) {
    return reg_arith(reg, imm, X64_OP_SUB, dst_buf);
}

static bool add_byte(u8 reg, i8 imm8, sized_buf *dst_buf) {
    /* ADD byte [reg], imm8 */
    return append_obj(dst_buf, (u8[]){INSTRUCTION(0x80, reg, imm8)}, 3);
}

static bool sub_byte(u8 reg, i8 imm8, sized_buf *dst_buf) {
    /* SUB byte [reg], imm8 */
    return append_obj(dst_buf, (u8[]){INSTRUCTION(0x80, 0x28 + reg, imm8)}, 3);
}

static bool zero_byte(u8 reg, sized_buf *dst_buf) {
    /* MOV byte [reg], 0 */
    return append_obj(dst_buf, (u8[]){INSTRUCTION(0x67, 0xc6, reg, 0x00)}, 4);
}

static const arch_funcs FUNCS = {
    set_reg,
    reg_copy,
    syscall,
    nop_loop_open,
    jump_zero,
    jump_not_zero,
    inc_reg,
    dec_reg,
    inc_byte,
    dec_byte,
    add_reg,
    sub_reg,
    add_byte,
    sub_byte,
    zero_byte,
};

static const arch_sc_nums SC_NUMS = {.read = 0, .write = 1, .exit = 60};

static const arch_registers REGS = {
    .sc_num = 00 /* RAX */,
    .arg1 = 07 /* RDI */,
    .arg2 = 06 /* RSI */,
    .arg3 = 02 /* RDX */,
    .bf_ptr = 03 /* RBX */,
};

const arch_inter X86_64_INTER = {
    .FUNCS = &FUNCS,
    .SC_NUMS = &SC_NUMS,
    .REGS = &REGS,
    .FLAGS = 0 /* no flags are defined for this architecture */,
    .ELF_ARCH = EM_X86_64,
    .ELF_DATA = ELFDATA2LSB,
};
#endif /* EAMBFC_TARGET_X86_64 */
