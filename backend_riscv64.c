/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */
#include <config.h>
#include <elf.h>
#include <types.h>

#include "arch_inter.h"
#include "err.h"
#include "serialize.h"
#include "util.h"

#if BFC_TARGET_RISCV64

/* enum of RISC-V registers used in this file, referred to by ABI mnemonic. */
enum RISCV_REGS {
    RISCV_T1 = 6,
    RISCV_S0 = 8,
    RISCV_A0 = 10,
    RISCV_A1 = 11,
    RISCV_A2 = 12,
    RISCV_A7 = 17
};

/* padding bytes to be replaced with an instruction within a u8 array. */
#define PAD_INSTRUCTION 0x00, 0x00, 0x00, 0x00

/* SPDX-SnippetCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only AND Apache-2.0 WITH LLVM-exception
 *
 * SPDX-SnippetCopyrightText: 2021 Alexander Pivovarov
 * SPDX-SnippetCopyrightText: 2021 Ben Shi
 * SPDX-SnippetCopyrightText: 2021 Craig Topper
 * SPDX-SnippetCopyrightText: 2021 Jim Lin
 * SPDX-SnippetCopyrightText: 2020 Simon Pilgrim
 * SPDX-SnippetCopyrightText: 2018 - 2019 Alex Bradbury
 * SPDX-SnippetCopyrightText: 2019 Chandler Carruth
 * SPDX-SnippetCopyrightText: 2019 Sam Elliott
 *
 * modification copyright 2025 Eli Array Minkoff
 *
 * This function is a C rewrite of a Rust port of the LLVM logic for
 * resolving the `li` (load-immediate) pseudo-instruction
 *
 * And yes, I did have to go through the git commit history of the original file
 * to find everyone who had contributed as of 2022, because I didn't want to
 * just go "Copyright 2018-2022 LLVM Contributors"
 * and call it a day - I believe firmly in giving credit where credit is do. */
static nonnull_args void encode_li(
    sized_buf *restrict code_buf, u8 reg, i64 val
) {
    u8 i_bytes[4];
    u32 lo12 = sign_extend(val, 12);
    if (bit_fits(val, 32)) {
        u32 hi20 = sign_extend(((u64)val + 0x800) >> 12, 20);
        if (hi20 && bit_fits(hi20, 6)) {
            u16 instr =
                0x6001 | (((hi20 & 0x20) | reg) << 7) | ((hi20 & 0x1f) << 2);
            serialize16le(instr, &i_bytes);
            append_obj(code_buf, &i_bytes, 2);
        } else if (hi20) {
            serialize32le((hi20 << 12) | (reg << 7) | 0x37, &i_bytes);
            append_obj(code_buf, &i_bytes, 4);
        }
        if (lo12 || !hi20) {
            if (bit_fits((i32)lo12, 6)) {
                /* if n == 0: `C.LI reg, lo6`
                 * else: `ADDIW reg, reg, lo6` */
                u16 instr = (hi20 ? 0x2001 : 0x4001) |
                            (((lo12 & 0x20) | reg) << 7) | ((lo12 & 0x1f) << 2);
                serialize16le(instr, &i_bytes);
                append_obj(code_buf, &i_bytes, 2);
            } else {
                /* if n != 0: `ADDIW reg, reg, lo12`
                 * else `ADDI reg, zero, lo12` */
                u32 opcode = hi20 ? 0x1b : 0x13;
                u32 rs1 = hi20 ? (((u32)reg) << 15) : 0;
                u32 instr = (lo12 << 20) | rs1 | (((u32)reg) << 7) | opcode;
                serialize32le(instr, &i_bytes);
                append_obj(code_buf, &i_bytes, 4);
            }
        }
        return;
    }
    i64 hi52 = ((u64)val + 0x800) >> 12;
    uint shift = trailing_0s(hi52) + 12;
    hi52 = sign_extend((u64)hi52 >> (shift - 12), 64 - shift);

    /* If the remaining bits don't fit in 12 bits, we might be able to reduce
     * the shift amount in order to use LUI which will zero the lower 12 bits */
    if (shift > 12 && (!bit_fits(hi52, 12)) &&
        bit_fits(((u64)hi52 << 12), 32)) {
        shift -= 12;
        hi52 = ((u64)hi52) << 12;
    }
    encode_li(code_buf, reg, hi52);
    if (shift) {
        /* C.SLLI reg, shift_amount */
        u16 instr = (((shift & 0x20) | reg) << 7) | ((shift & 0x1f) << 2) | 2;
        serialize16le(instr, &i_bytes);
        append_obj(code_buf, i_bytes, 2);
    }
    if (lo12 && bit_fits((i16)lo12, 6)) {
        /* C.ADDI reg, reg, lo12 */
        u16 instr =
            0x0001 | (((lo12 & 0x20) | reg) << 7) | ((lo12 & 0x1f) << 2);
        serialize16le(instr, &i_bytes);
        append_obj(code_buf, i_bytes, 2);
    } else if (lo12) {
        /* ADDI, reg, reg, lo12 */
        u32 instr = (lo12 << 20) | ((u32)reg << 15) | ((u32)reg << 7) | 0x13;
        serialize32le(instr, &i_bytes);
        append_obj(code_buf, i_bytes, 4);
    }
}

/* SPDX-SnippetEnd */

/* return a u32 containing the value of the instruction to store the lowest byte
 * in t1 at the address pointed to by addr_reg, suitable to pass to
 * serialize32le */
static u32 store_to_byte(u8 addr) {
    /* SB */
    return (((u32)RISCV_T1) << 20) | (((u32)addr) << 15) | 0x23;
}

/* return a u32 containing the value of the instruction to load the byte at the
 * address pointed to by addr in t1 suitable to pass to serialize32le */
static u32 load_from_byte(u8 addr) {
    /* LB */
    return (((u32)addr) << 15) | (((u32)RISCV_T1) << 7) | 0x03;
}

#define JUMP_SIZE 12

static bool cond_jump(
    u8 reg,
    i64 distance,
    bool eq,
    char dst[restrict JUMP_SIZE],
    bf_comp_err *restrict err
) {
    /* there are 2 types of instructions used here for control flow - branches,
     * which can conditionally move up to 4 KiB away, and jumps, which
     * unconditionally move up to 1MiB away. The former is too short, and the
     * latter is unconditional, so the solution is to use an inverted branch
     * condition and set it to branch over the unconditional jump. Ugly, but it
     * works.
     *
     * There are C.BNEZ and C.BEQZ instructions that could branch smaller
     * distances and always compare their operand register against the zero
     * register, but they only work with a specific subset of registers, all of
     * which are non-volatile. */
    if (!bit_fits(distance, 21)) {
        *err = basic_err(
            BF_ERR_JUMP_TOO_LONG,
            "offset is outside the range of possible 21-bit signed values"
        );
        return false;
    }
    if ((distance % 2) != 0) {
        internal_err(
            BF_ICE_INVALID_JUMP_ADDRESS,
            "offset is an invalid address offset (offset % 2 != 0)"
        );
        /* internal_err never returns, so this will not run */
        return false;
    }
    u32 jump_dist = distance + 4;
    serialize32le(load_from_byte(reg), dst);
    /* `BNEZ t1, 8` if comp_type == Eq, otherwise `BEQZ t1, 8` */
    serialize32le(eq ? 0x31463 : 0x30463, &dst[4]);
    /* J-type is a variant of U-type with the bits scrambled around to simplify
     * hardware implementation at the expense of compiler/assembler
     * implementation. */
    u32 encoded_dist = ((jump_dist & (1 << 20)) << 11) |
                       ((jump_dist & 0x7fe) << 20) |
                       ((jump_dist & (1 << 11)) << 9) | (jump_dist & 0xff000);
    serialize32le(encoded_dist | 0x6f, &dst[8]);
    return true;
}

static nonnull_args void set_reg(u8 reg, i64 imm, sized_buf *restrict dst_buf) {
    encode_li(dst_buf, reg, imm);
}

static nonnull_args void reg_copy(u8 dst, u8 src, sized_buf *restrict dst_buf) {
    /* C.MV dst, src */
    u16 instr = 0x8002 | (((u16)dst) << 7) | (((u16)src) << 2);
    append_obj(dst_buf, (u8[]){instr & 0xff, (instr & 0xff00) >> 8}, 2);
}

static nonnull_args void syscall(sized_buf *restrict dst_buf) {
    /* ecall */
    append_obj(dst_buf, (u8[]){0x73, 0, 0, 0}, 4);
}

static nonnull_args void pad_loop_open(sized_buf *restrict dst_buf) {
    /* ILLEGAL; NOP; NOP */
    uchar instr_seq[3][4] = {{0x0}, {0x13}, {0x13}};
    append_obj(dst_buf, instr_seq, 12);
}

static nonnull_args bool jump_open(
    u8 reg,
    i64 offset,
    sized_buf *restrict dst_buf,
    size_t index,
    bf_comp_err *restrict err
) {
    return cond_jump(reg, offset, true, &dst_buf->buf[index], err);
}

static nonnull_args bool jump_close(
    u8 reg, i64 offset, sized_buf *restrict dst_buf, bf_comp_err *restrict err
) {
    return cond_jump(reg, offset, false, sb_reserve(dst_buf, JUMP_SIZE), err);
}

static nonnull_args void inc_reg(u8 reg, sized_buf *restrict dst_buf) {
    /* c.addi reg, 1 */
    u16 instr = 5 | (((u16)reg) << 7);
    append_obj(dst_buf, (u8[]){instr & 0xff, (instr & 0xff00) >> 8}, 2);
}

static nonnull_args void dec_reg(u8 reg, sized_buf *restrict dst_buf) {
    /* c.addi reg, -1 */
    u16 instr = 0x107d | (((u16)reg) << 7);
    append_obj(dst_buf, (u8[]){instr & 0xff, (instr & 0xff00) >> 8}, 2);
}

static nonnull_args void inc_byte(u8 reg, sized_buf *restrict dst_buf) {
    u8 i_bytes[10] = {
        PAD_INSTRUCTION,
        /* c.addi t1, 1 */
        0x05,
        0x03,
        PAD_INSTRUCTION,
    };
    serialize32le(load_from_byte(reg), &i_bytes);
    serialize32le(store_to_byte(reg), &i_bytes[6]);
    append_obj(dst_buf, i_bytes, 10);
}

static nonnull_args void dec_byte(u8 reg, sized_buf *restrict dst_buf) {
    u8 i_bytes[10] = {
        PAD_INSTRUCTION,
        /* c.addi t1, -1 */
        0x7d,
        0x13,
        PAD_INSTRUCTION,
    };
    serialize32le(load_from_byte(reg), &i_bytes);
    serialize32le(store_to_byte(reg), &i_bytes[6]);
    append_obj(dst_buf, i_bytes, 10);
}

static nonnull_args void add_reg(u8 reg, u64 imm, sized_buf *restrict dst_buf) {
    if (!imm) return;
    if (bit_fits(imm, 6)) {
        /* c.addi reg, imm */
        u16 c_addi = (((imm & 0x20) | reg) << 7) | ((imm & 0x1f) << 2) | 1;
        append_obj(dst_buf, (u8[]){c_addi & 0xff, (c_addi & 0xff00) >> 8}, 2);
        return;
    }
    if (bit_fits(imm, 12)) {
        /* addi reg, reg, imm */
        u8 i_bytes[4];
        serialize32le(
            (((u32)imm) << 20) | (((u32)reg) << 15) | (((u32)reg) << 7) | 0x13,
            &i_bytes
        );
        append_obj(dst_buf, i_bytes, 4);
        return;
    }
    /* c.add reg, t1 */
    u16 add_regs = 0x9002 | (((u16)reg) << 7) | (((u16)RISCV_T1) << 2);
    encode_li(dst_buf, RISCV_T1, imm);
    append_obj(dst_buf, (u8[]){add_regs & 0xff, (add_regs & 0xff00) >> 8}, 2);
}

static nonnull_args void sub_reg(u8 reg, u64 imm, sized_buf *restrict dst_buf) {
    /* adding and subtracting INT64_MIN result in the same value, but negating
     * INT64_MIN is undefined behavior, so this is the way to go. */
    i64 neg_imm = imm;
    if (neg_imm != INT64_MIN) neg_imm = -neg_imm;
    add_reg(reg, neg_imm, dst_buf);
}

static nonnull_args void add_byte(
    u8 reg, u8 imm8, sized_buf *restrict dst_buf
) {
    if (!imm8) return;
    u8 i_bytes[12];
    uint sz;
    i16 imm = sign_extend(imm8, 8);
    serialize32le(load_from_byte(reg), i_bytes);
    if (bit_fits(imm, 6)) {
        sz = 10;
        /* c.addi t1, imm8 */
        serialize16le(
            (((imm & 0x20) | RISCV_T1) << 7) | ((imm & 0x1f) << 2) | 1,
            &i_bytes[4]
        );
    } else {
        sz = 12;
        /* addi t1, t1, imm8 */
        serialize32le(
            (((u32)imm) << 20) | (((u32)RISCV_T1) << 15) |
                (((u32)RISCV_T1) << 7) | 0x13,
            &i_bytes[4]
        );
    }
    serialize32le(store_to_byte(reg), &i_bytes[sz - 4]);
    append_obj(dst_buf, i_bytes, sz);
}

static nonnull_args void sub_byte(
    u8 reg, u8 imm8, sized_buf *restrict dst_buf
) {
    if (!imm8) return;
    u8 i_bytes[12];
    uint sz;
    i16 imm = -sign_extend(imm8, 8);
    serialize32le(load_from_byte(reg), i_bytes);
    if (bit_fits(imm, 6)) {
        sz = 10;
        /* c.addi t1, imm8 */
        serialize16le(
            (((imm & 0x20) | RISCV_T1) << 7) | ((imm & 0x1f) << 2) | 1,
            &i_bytes[4]
        );
    } else {
        sz = 12;
        /* addi t1, t1, imm8 */
        serialize32le(
            (((u32)imm) << 20) | (((u32)RISCV_T1) << 15) |
                (((u32)RISCV_T1) << 7) | 0x13,
            &i_bytes[4]
        );
    }
    serialize32le(store_to_byte(reg), &i_bytes[sz - 4]);
    append_obj(dst_buf, i_bytes, sz);
}

static nonnull_args void zero_byte(u8 reg, sized_buf *restrict dst_buf) {
    u32 instr = 0x23 | (((u32)reg) << 15);
    u8 i_bytes[4];
    serialize32le(instr, i_bytes);
    append_obj(dst_buf, i_bytes, 4);
}

const arch_inter RISCV64_INTER = {
    .sc_read = 63,
    .sc_write = 64,
    .sc_exit = 93,
    .set_reg = set_reg,
    .reg_copy = reg_copy,
    .syscall = syscall,
    .pad_loop_open = pad_loop_open,
    .jump_open = jump_open,
    .jump_close = jump_close,
    .inc_reg = inc_reg,
    .dec_reg = dec_reg,
    .inc_byte = inc_byte,
    .dec_byte = dec_byte,
    .add_reg = add_reg,
    .sub_reg = sub_reg,
    .add_byte = add_byte,
    .sub_byte = sub_byte,
    .zero_byte = zero_byte,
    /* EF_RISCV_RVC | EF_RISCV_FLOAT_ABI_DOUBLE (chosen to match Debian) */
    .flags = 5,
    .elf_arch = 243 /* EM_RISCV */,
    .elf_data = BYTEORDER_LSB,
    .reg_sc_num = RISCV_A7,
    .reg_arg1 = RISCV_A0,
    .reg_arg2 = RISCV_A1,
    .reg_arg3 = RISCV_A2,
    .reg_bf_ptr = RISCV_S0,
};

#ifdef BFC_TEST

/* internal */
#include "unit_test.h"

#define REF RISCV64_DIS

static void test_set_reg_32(void) {
    sized_buf sb = newbuf(32);
    sized_buf dis = newbuf(168);
    set_reg(RISCV_A0, 0, &sb);
    CU_ASSERT_EQUAL(sb.sz, 2);
    set_reg(RISCV_A1, 1, &sb);
    CU_ASSERT_EQUAL(sb.sz, 4);
    set_reg(RISCV_A2, -2, &sb);
    CU_ASSERT_EQUAL(sb.sz, 6);
    set_reg(RISCV_A7, 0x123, &sb);
    CU_ASSERT_EQUAL(sb.sz, 10);
    set_reg(RISCV_A0, -0x123, &sb);
    CU_ASSERT_EQUAL(sb.sz, 14);
    set_reg(RISCV_S0, 0x100000, &sb);
    CU_ASSERT_EQUAL(sb.sz, 18);
    set_reg(RISCV_A7, 0x123456, &sb);
    CU_ASSERT_EQUAL(sb.sz, 26);
    set_reg(RISCV_A0, 0x1000, &sb);
    CU_ASSERT_EQUAL(sb.sz, 28);
    set_reg(RISCV_A1, 0x1001, &sb);
    CU_ASSERT_EQUAL(sb.sz, 32);
    DISASM_TEST(
        sb,
        dis,
        "li a0, 0x0\n"
        "li a1, 0x1\n"
        "li a2, -0x2\n"
        "li a7, 0x123\n"
        "li a0, -0x123\n"
        "lui s0, 0x100\n"
        "lui a7, 0x123\n"
        "addiw a7, a7, 0x456\n"
        "lui a0, 0x1\n"
        "lui a1, 0x1\n"
        "addiw a1, a1, 0x1\n"
    );
    free(dis.buf);
    free(sb.buf);
}

static void test_set_reg_64(void) {
    sized_buf dis = newbuf(1024);
    sized_buf sb = newbuf(124);

    char *expected_disasm = checked_malloc(1024);
    char *disasm_p = expected_disasm;
    size_t expected_len = 0;
    for (ifast_64 val = ((i64)INT32_MAX) + 1; val < INT64_MAX / 2; val <<= 1) {
        set_reg(RISCV_A7, val, &sb);
        u8 shift_lvl = trailing_0s(val);
        disasm_p +=
            sprintf(disasm_p, "li a7, 0x1\nslli a7, a7, 0x%x\n", shift_lvl);
        expected_len += 4;
        CU_ASSERT_EQUAL(sb.sz, expected_len);
    }
    CU_ASSERT_EQUAL(expected_len, 124);
    DISASM_TEST(sb, dis, expected_disasm);
    free(expected_disasm);

    /* worst-case scenario is alternating bits. 0b0101 is 0x5. */
    /* first, just a single sequence, but it'll need to be shifted */
    set_reg(RISCV_A7, INT64_C(0x5555) << 24, &sb);
    CU_ASSERT_EQUAL(sb.sz, 6);
    DISASM_TEST(sb, dis, "lui a7, 0x5555\nslli a7, a7, 0xc\n");

    set_reg(RISCV_S0, INT64_C(0x555555555555), &sb);
    set_reg(RISCV_A7, INT64_C(-0x555555555555), &sb);
    DISASM_TEST(
        sb,
        dis,
        /* this is what LLVM 19 generates for these instructions:
         * ###
         * llvm-mc --triple=riscv64-linux-gnu -mattr=+c --print-imm-hex - <<<EOF
         * li s0, 0x555555555555
         * li a7, -0x555555555555
         * EOF
         * ### */
        "lui s0, 0x555\n"
        "addiw s0, s0, 0x555\n"
        "slli s0, s0, 0xc\n"
        "addi s0, s0, 0x555\n"
        "slli s0, s0, 0xc\n"
        "addi s0, s0, 0x555\n"
        "lui a7, 0xffaab\n"
        "addiw a7, a7, -0x555\n"
        "slli a7, a7, 0xc\n"
        "addi a7, a7, -0x555\n"
        "slli a7, a7, 0xc\n"
        "addi a7, a7, -0x555\n"
    );

    /* again, but with 64-bit rather than 48-bit values */
    set_reg(RISCV_S0, INT64_C(0x5555555555555555), &sb);
    set_reg(RISCV_A7, INT64_C(-0x5555555555555555), &sb);
    DISASM_TEST(
        sb,
        dis,
        /* this is what LLVM 19 generates for these instructions:
         * ###
         * llvm-mc --triple=riscv64-linux-gnu -mattr=+c --print-imm-hex - <<<EOF
         * li s0, 0x5555555555555555
         * li a7, -0x5555555555555555
         * EOF
         * ### */
        "lui s0, 0x5555\n"
        "addiw s0, s0, 0x555\n"
        "slli s0, s0, 0xc\n"
        "addi s0, s0, 0x555\n"
        "slli s0, s0, 0xc\n"
        "addi s0, s0, 0x555\n"
        "slli s0, s0, 0xc\n"
        "addi s0, s0, 0x555\n"
        "lui a7, 0xfaaab\n"
        "addiw a7, a7, -0x555\n"
        "slli a7, a7, 0xc\n"
        "addi a7, a7, -0x555\n"
        "slli a7, a7, 0xc\n"
        "addi a7, a7, -0x555\n"
        "slli a7, a7, 0xc\n"
        "addi a7, a7, -0x555\n"
    );

    free(dis.buf);
    free(sb.buf);
}

static void test_compressed_set_reg_64(void) {
    sized_buf sb = newbuf(6);
    sized_buf dis = newbuf(24);
    sized_buf fakesb;
    set_reg(RISCV_A1, INT64_C(0xf00000010), &sb);
    /* fatal variant because the following trickery with fakesb would be
     * problematic otherwise. */
    CU_ASSERT_EQUAL_FATAL(sb.sz, 6);

    /* make sure that it's encoded using the following instructions, and that
     * each of the instructions is exactly 2 bytes in size */
    fakesb.buf = sb.buf;
    fakesb.sz = 2;
    fakesb.capacity = 2;
    DISASM_TEST(fakesb, dis, "li a1, 0xf\n");

    fakesb.sz = 2;
    fakesb.buf += 2;
    memset(dis.buf, 0, dis.sz);
    DISASM_TEST(fakesb, dis, "slli a1, a1, 0x20\n");

    fakesb.sz = 2;
    fakesb.buf += 2;
    memset(dis.buf, 0, dis.sz);
    DISASM_TEST(fakesb, dis, "addi a1, a1, 0x10\n");

    free(dis.buf);
    free(sb.buf);
}

static void test_syscall(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(8);

    syscall(&sb);
    DISASM_TEST(sb, dis, "ecall\n");

    free(dis.buf);
    free(sb.buf);
}

static void test_reg_copies(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(16);

    reg_copy(RISCV_A1, RISCV_S0, &sb);
    DISASM_TEST(sb, dis, "mv a1, s0\n");

    reg_copy(RISCV_S0, RISCV_A7, &sb);
    DISASM_TEST(sb, dis, "mv s0, a7\n");

    free(dis.buf);
    free(sb.buf);
}

static void test_load_and_store(void) {
    sized_buf sb = newbuf(8);
    sized_buf dis = newbuf(40);

    serialize32le(load_from_byte(RISCV_A0), sb_reserve(&sb, 4));
    serialize32le(store_to_byte(RISCV_A0), sb_reserve(&sb, 4));
    DISASM_TEST(sb, dis, "lb t1, 0x0(a0)\nsb t1, 0x0(a0)\n");

    free(dis.buf);
    free(sb.buf);
}

static void test_zero_byte(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(24);

    zero_byte(RISCV_A2, &sb);
    DISASM_TEST(sb, dis, "sb zero, 0x0(a2)\n");

    free(dis.buf);
    free(sb.buf);
}

static void test_jump_pad(void) {
    sized_buf sb = newbuf(12);
    sized_buf dis = newbuf(16);

    pad_loop_open(&sb);
    CU_ASSERT_EQUAL(sb.sz, 12);
    /* the defined illegal instruction 0x0000_0000 is interpreted as two
     * unimplemented 0x0000 instructions by LLVM */
    DISASM_TEST(sb, dis, "unimp\nunimp\nnop\nnop\n");

    free(dis.buf);
    free(sb.buf);
}

static void test_successful_jumps(void) {
    sized_buf sb = newbuf(12);
    sized_buf dis = newbuf(72);
    bf_comp_err e;
    sb_reserve(&sb, JUMP_SIZE);
    jump_open(RISCV_S0, 32, &sb, 0, &e);
    jump_close(RISCV_S0, -32, &sb, &e);
    DISASM_TEST(
        sb,
        dis,
        "lb t1, 0x0(s0)\n"
        "bnez t1, 0x8\n"
        "j 0x24\n"
        "lb t1, 0x0(s0)\n"
        "beqz t1, 0x8\n"
        "j -0x1c\n"
    );

    free(dis.buf);
    free(sb.buf);
}

static void test_inc_dec(void) {
    sized_buf sb = newbuf(24);
    sized_buf dis = newbuf(136);

    inc_byte(RISCV_S0, &sb);
    CU_ASSERT_EQUAL(sb.sz, 10);

    dec_byte(RISCV_S0, &sb);
    CU_ASSERT_EQUAL(sb.sz, 20);

    inc_reg(RISCV_S0, &sb);
    CU_ASSERT_EQUAL(sb.sz, 22);

    dec_reg(RISCV_S0, &sb);
    CU_ASSERT_EQUAL(sb.sz, 24);

    DISASM_TEST(
        sb,
        dis,
        "lb t1, 0x0(s0)\n"
        "addi t1, t1, 0x1\n"
        "sb t1, 0x0(s0)\n"
        "lb t1, 0x0(s0)\n"
        "addi t1, t1, -0x1\n"
        "sb t1, 0x0(s0)\n"
        "addi s0, s0, 0x1\n"
        "addi s0, s0, -0x1\n"
    );

    free(dis.buf);
    free(sb.buf);
}

static void sub_reg_is_neg_add_reg(void) {
    sized_buf a, b;
    a = newbuf(32);
    b = newbuf(32);

    sub_reg(RISCV64_INTER.reg_bf_ptr, 0, &a);
    add_reg(RISCV_S0, 0, &b);
    CU_ASSERT_EQUAL_FATAL(a.sz, b.sz);
    CU_ASSERT(memcmp(a.buf, b.buf, a.sz) == 0);
    for (ufast_8 i = 0; i < 63; i++) {
        a.sz = 0;
        b.sz = 0;
        sub_reg(RISCV64_INTER.reg_bf_ptr, INT64_C(1) << i, &a);
        add_reg(RISCV_S0, -(INT64_C(1) << i), &b);
        CU_ASSERT_EQUAL_FATAL(a.sz, b.sz);
        CU_ASSERT(memcmp(a.buf, b.buf, a.sz) == 0);
    }

    free(a.buf);
    free(b.buf);
}

static void test_add_reg(void) {
    sized_buf sb = newbuf(12);
    sized_buf alt = newbuf(10);
    sized_buf dis = newbuf(40);
    sized_buf fakesb;

    add_reg(RISCV_S0, 0, &sb);
    CU_ASSERT_EQUAL(sb.sz, 0);

    add_reg(RISCV_S0, 0x12, &sb);
    CU_ASSERT_EQUAL(sb.sz, 2);

    add_reg(RISCV_S0, 0x123, &sb);
    CU_ASSERT_EQUAL(sb.sz, 6);

    DISASM_TEST(sb, dis, "addi s0, s0, 0x12\naddi s0, s0, 0x123\n");

    add_reg(RISCV_S0, 0xdeadbeef, &sb);
    encode_li(&alt, RISCV_T1, 0xdeadbeef);
    CU_ASSERT_EQUAL_FATAL(sb.sz, alt.sz + 2);
    CU_ASSERT(memcmp(sb.buf, alt.buf, alt.sz) == 0);
    fakesb.sz = 2;
    fakesb.capacity = 2;
    fakesb.buf = sb.buf + alt.sz;
    memset(dis.buf, 0, dis.sz);
    DISASM_TEST(fakesb, dis, "add s0, s0, t1\n");

    free(sb.buf);
    free(alt.buf);
    free(dis.buf);
}

static void test_add_sub_byte(void) {
    sized_buf sb = newbuf(28);
    sized_buf dis = newbuf(96);

    add_byte(RISCV64_INTER.reg_arg1, 0, &sb);
    sub_byte(RISCV64_INTER.reg_arg1, 0, &sb);
    CU_ASSERT_EQUAL(sb.sz, 0);

    add_byte(RISCV64_INTER.reg_arg2, 0x10, &sb);
    sub_byte(RISCV64_INTER.reg_arg2, 0x10, &sb);
    CU_ASSERT_EQUAL(sb.sz, 20);
    DISASM_TEST(
        sb,
        dis,
        "lb t1, 0x0(a1)\n"
        "addi t1, t1, 0x10\n"
        "sb t1, 0x0(a1)\n"
        "lb t1, 0x0(a1)\n"
        "addi t1, t1, -0x10\n"
        "sb t1, 0x0(a1)\n"
    );
    memset(dis.buf, 0, dis.sz);

    /* 0x70 is too large to fit in the compressed add instruction, so 2 extra
     * bytes per addi are needed */
    add_byte(RISCV64_INTER.reg_arg2, 0x70, &sb);
    sub_byte(RISCV64_INTER.reg_arg2, 0x70, &sb);
    CU_ASSERT_EQUAL(sb.sz, 24);
    DISASM_TEST(
        sb,
        dis,
        "lb t1, 0x0(a1)\n"
        "addi t1, t1, 0x70\n"
        "sb t1, 0x0(a1)\n"
        "lb t1, 0x0(a1)\n"
        "addi t1, t1, -0x70\n"
        "sb t1, 0x0(a1)\n"
    );
    memset(dis.buf, 0, dis.sz);

    /* if the imm is >= 0x80, it will become negative due to the casting that's
     * done, but will have the same byte value once truncated down. */
    add_byte(RISCV64_INTER.reg_arg3, 0x80, &sb);
    sub_byte(RISCV64_INTER.reg_arg3, 0x80, &sb);
    CU_ASSERT_EQUAL((i8)(INT16_C(1) + 0x80), (i8)(INT16_C(1) - 0x80));
    (void)0;
    DISASM_TEST(
        sb,
        dis,
        "lb t1, 0x0(a2)\n"
        "addi t1, t1, -0x80\n"
        "sb t1, 0x0(a2)\n"
        "lb t1, 0x0(a2)\n"
        "addi t1, t1, 0x80\n"
        "sb t1, 0x0(a2)\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_bad_jump_offset(void) {
    testing_err = TEST_INTERCEPT;
    int returned_err;
    if ((returned_err = setjmp(etest_stack))) {
        CU_ASSERT_EQUAL(BF_ICE_INVALID_JUMP_ADDRESS, returned_err >> 1);
        testing_err = NOT_TESTING;
        return;
    }
    bf_comp_err e;
    char dst[JUMP_SIZE];
    jump_close(
        0, 31, &(sized_buf){.buf = dst, .sz = 0, .capacity = JUMP_SIZE}, &e
    );
    longjmp(etest_stack, (BF_NOT_ERR << 1) | 1);
}

static void test_jump_too_long(void) {
    bf_comp_err e;
    sized_buf sb = newbuf(JUMP_SIZE);
    CU_ASSERT_FALSE(jump_close(0, 1 << 23, &sb, &e));
    CU_ASSERT_EQUAL(e.id, BF_ERR_JUMP_TOO_LONG);
    free(sb.buf);
}

CU_pSuite register_riscv64_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, test_set_reg_32);
    ADD_TEST(suite, test_set_reg_64);
    ADD_TEST(suite, test_compressed_set_reg_64);
    ADD_TEST(suite, test_syscall);
    ADD_TEST(suite, test_reg_copies);
    ADD_TEST(suite, test_load_and_store);
    ADD_TEST(suite, test_zero_byte);
    ADD_TEST(suite, test_jump_pad);
    ADD_TEST(suite, test_successful_jumps);
    ADD_TEST(suite, test_inc_dec);
    ADD_TEST(suite, sub_reg_is_neg_add_reg);
    ADD_TEST(suite, test_add_reg);
    ADD_TEST(suite, test_add_sub_byte);
    ADD_TEST(suite, test_bad_jump_offset);
    ADD_TEST(suite, test_jump_too_long);
    return suite;
}

#endif /* BFC_TEST */

#endif /* BFC_TARGET_RISCV64 */
