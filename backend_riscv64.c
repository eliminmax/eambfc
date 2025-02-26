/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */
#include "arch_inter.h"
#include "compat/elf.h"
#include "config.h"
#include "err.h"
#include "serialize.h"
#include "types.h"
#include "util.h"

#if BFC_TARGET_RISCV64

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
bool encode_li(sized_buf *code_buf, u8 reg, i64 val) {
    u8 i_bytes[4];
    u32 lo12 = sign_extend(val, 12);
    if (bit_fits(val, 32)) {
        i32 hi20 = sign_extend(((u64)val + 0x800) >> 12, 20);
        if (hi20 && bit_fits(hi20, 6)) {
            u16 instr =
                0x6001 | (((hi20 & 0x20) | reg) << 7) | ((hi20 & 0x1f) << 2);
            serialize16le(instr, &i_bytes);
            if (!append_obj(code_buf, &i_bytes, 2)) return false;
        } else if (hi20) {
            serialize32le((hi20 << 12) | (reg << 7) | 0x37, &i_bytes);
            if (!append_obj(code_buf, &i_bytes, 4)) return false;
        }
        if (lo12 || !hi20) {
            if (bit_fits((i32)lo12, 6)) {
                /* if n == 0: `C.LI reg, lo6`
                 * else: `ADDIW reg, reg, lo6` */
                u16 instr = (hi20 ? 0x2001 : 0x4001) |
                            (((lo12 & 0x20) | reg) << 7) | ((lo12 & 0x1f) << 2);
                serialize16le(instr, &i_bytes);
                if (!append_obj(code_buf, &i_bytes, 2)) return false;
            } else {
                /* if n != 0: `ADDIW reg, reg, lo12`
                 * else `ADDI reg, zero, lo12` */
                u32 opcode = hi20 ? 0x1b : 0x13;
                u32 rs1 = hi20 ? (((u32)reg) << 15) : 0;
                u32 instr = (lo12 << 20) | rs1 | (((u32)reg) << 7) | opcode;
                serialize32le(instr, &i_bytes);
                if (!append_obj(code_buf, &i_bytes, 4)) return false;
            }
        }
        return true;
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
        if (!append_obj(code_buf, i_bytes, 2)) return false;
    }
    if (lo12 && bit_fits((i16)lo12, 6)) {
        /* C.ADDI reg, reg, lo12 */
        u16 instr =
            0x0001 | (((lo12 & 0x20) | reg) << 7) | ((lo12 & 0x1f) << 2);
        serialize16le(instr, &i_bytes);
        return append_obj(code_buf, i_bytes, 2);
    } else if (lo12) {
        /* ADDI, reg, reg, lo12 */
        u32 instr = (lo12 << 20) | ((u32)reg << 15) | ((u32)reg << 7) | 0x13;
        serialize32le(instr, &i_bytes);
        return append_obj(code_buf, i_bytes, 4);
    }
    return true;
}

/* SPDX-SnippetEnd */

/* t1 temporary register used as a scratch register within certain operations */
static const u8 TEMP_REG = 6;

/* return a u32 containing the value of the instruction to store the lowest byte
 * in TEMP_REG at the address pointed to by addr_reg, suitable to pass to
 * serialize32le */
static u32 store_to_byte(u8 addr) {
    /* SB */
    return (((u32)TEMP_REG) << 20) | (((u32)addr) << 15) | 0x23;
}

/* return a u32 containing the value of the instruction to load the byte at the
 * address pointed to by addr in TEMPREG suitable to pass to serialize32le */
static u32 load_from_byte(u8 addr) {
    /* LB */
    return (((u32)addr) << 15) | (((u32)TEMP_REG) << 7) | 0x03;
}

static bool cond_jump(u8 reg, i64 distance, bool eq, sized_buf *dst_buf) {
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
        basic_err(
            "JUMP_TOO_LONG",
            "offset is outside the range of possible 21-bit signed values"
        );
        return false;
    }
    if ((distance % 2) != 0) {
        internal_err(
            "INVALID_JUMP_ADDRESS",
            "offset is an invalid address offset (offset % 2 != 0)"
        );
        /* internal_err never returns, so this will not run */
        return false;
    }
    u8 i_bytes[12];
    u32 jump_dist = distance + 4;
    serialize32le(load_from_byte(reg), i_bytes);
    // `BNEZ t1, 8` if comp_type == Eq, otherwise `BEQZ t1, 8`
    serialize32le(eq ? 0x31463 : 0x30463, &i_bytes[4]);
    /* J-type is a variant of U-type with the bits scrambled around to simplify
     * hardware implementation at the expense of compiler/assembler
     * implementation. */
    u32 encoded_dist = ((jump_dist & (1 << 20)) << 11) |
                       ((jump_dist & 0x7fe) << 20) |
                       ((jump_dist & (1 << 11)) << 9) | (jump_dist & 0xff000);
    serialize32le(encoded_dist | 0x6f, &i_bytes[8]);
    return append_obj(dst_buf, i_bytes, 12);
}

static bool set_reg(u8 reg, i64 imm, sized_buf *dst_buf) {
    return encode_li(dst_buf, reg, imm);
}

static bool reg_copy(u8 dst, u8 src, sized_buf *dst_buf) {
    /* C.MV dst, src */
    u16 instr = 0x8002 | (((u16)dst) << 7) | (((u16)src) << 2);
    return append_obj(dst_buf, (u8[]){instr & 0xff, (instr & 0xff00) >> 8}, 2);
}

static bool syscall(sized_buf *dst_buf) {
    /* ecall */
    return append_obj(dst_buf, (u8[]){0x73, 0, 0, 0}, 4);
}

static bool nop_loop_open(sized_buf *dst_buf) {
    /* nop */
#define NOP 0x13, 0, 0, 0
    return append_obj(dst_buf, (u8[]){NOP, NOP, NOP}, 12);
#undef NOP
}

static bool jump_zero(u8 reg, i64 offset, sized_buf *dst_buf) {
    return cond_jump(reg, offset, true, dst_buf);
}

static bool jump_not_zero(u8 reg, i64 offset, sized_buf *dst_buf) {
    return cond_jump(reg, offset, false, dst_buf);
}

static bool inc_reg(u8 reg, sized_buf *dst_buf) {
    /* c.addi reg, 1 */
    u16 instr = 5 | (((u16)reg) << 7);
    return append_obj(dst_buf, (u8[]){instr & 0xff, (instr & 0xff00) >> 8}, 2);
}

static bool dec_reg(u8 reg, sized_buf *dst_buf) {
    /* c.addi reg, -1 */
    u16 instr = 0x107d | (((u16)reg) << 7);
    return append_obj(dst_buf, (u8[]){instr & 0xff, (instr & 0xff00) >> 8}, 2);
}

static bool inc_byte(u8 reg, sized_buf *dst_buf) {
    u8 i_bytes[10] = {
        PAD_INSTRUCTION,
        /* c.addi t1, 1 */
        0x05,
        0x03,
        PAD_INSTRUCTION,
    };
    serialize32le(load_from_byte(reg), &i_bytes);
    serialize32le(store_to_byte(reg), &i_bytes[6]);
    return append_obj(dst_buf, i_bytes, 10);
}

static bool dec_byte(u8 reg, sized_buf *dst_buf) {
    u8 i_bytes[10] = {
        PAD_INSTRUCTION,
        /* c.addi t1, -1 */
        0x7d,
        0x13,
        PAD_INSTRUCTION,
    };
    serialize32le(load_from_byte(reg), &i_bytes);
    serialize32le(store_to_byte(reg), &i_bytes[6]);
    return append_obj(dst_buf, i_bytes, 10);
}

static bool add_reg(u8 reg, u64 imm, sized_buf *dst_buf) {
    if (!imm) return true;
    if (bit_fits(imm, 6)) {
        /* c.addi reg, imm */
        u16 c_addi = (((imm & 0x20) | reg) << 7) | ((imm & 0x1f) << 2) | 1;
        return append_obj(
            dst_buf, (u8[]){c_addi & 0xff, (c_addi & 0xff00) >> 8}, 2
        );
    }
    if (bit_fits(imm, 12)) {
        /* addi reg, reg, imm */
        u8 i_bytes[4];
        serialize32le(
            (((u32)imm) << 20) | (((u32)reg) << 15) | (((u32)reg) << 7) | 0x13,
            &i_bytes
        );
        return append_obj(dst_buf, i_bytes, 4);
    }
    /* c.add reg, TEMP_REG */
    u16 add_regs = 0x9002 | (((u16)reg) << 7) | (((u16)TEMP_REG) << 2);
    return encode_li(dst_buf, TEMP_REG, imm) &&
           append_obj(
               dst_buf, (u8[]){add_regs & 0xff, (add_regs & 0xff00) >> 8}, 2
           );
}

static bool sub_reg(u8 reg, u64 imm, sized_buf *dst_buf) {
    /* adding and subtracting INT64_MIN result in the same value, but negating
     * INT64_MIN is undefined behavior, so this is the way to go. */
    i64 neg_imm = imm;
    if (neg_imm != INT64_MIN) neg_imm = -neg_imm;
    return add_reg(reg, neg_imm, dst_buf);
}

static bool add_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    if (!imm8) return true;
    u8 i_bytes[12];
    uint sz;
    i8 imm_s = imm8;
    serialize32le(load_from_byte(reg), i_bytes);
    if (bit_fits(imm_s, 6)) {
        sz = 10;
        /* c.addi TEMP_REG, imm8 */
        serialize16le(
            (((imm_s & 0x20) | TEMP_REG) << 7) | ((imm_s & 0x1f) << 2) | 1,
            &i_bytes[4]
        );
    } else {
        sz = 12;
        /* addi TEMP_REG, TEMP_REG, imm8 */
        serialize32le(
            (((u32)imm_s) << 20) | (((u32)TEMP_REG) << 15) |
                (((u32)TEMP_REG) << 7) | 0x13,
            &i_bytes[4]
        );
    }
    serialize32le(store_to_byte(reg), &i_bytes[sz - 4]);
    return append_obj(dst_buf, i_bytes, sz);
}

static bool sub_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    if (!imm8) return true;
    u8 i_bytes[12];
    uint sz;
    i8 imm_s = -((i16)imm8);
    serialize32le(load_from_byte(reg), i_bytes);
    if (bit_fits(imm_s, 6)) {
        sz = 10;
        /* c.addi TEMP_REG, imm8 */
        serialize16le(
            (((imm_s & 0x20) | TEMP_REG) << 7) | ((imm_s & 0x1f) << 2) | 1,
            &i_bytes[4]
        );
    } else {
        sz = 12;
        /* addi TEMP_REG, TEMP_REG, imm8 */
        serialize32le(
            (((u32)imm_s) << 20) | (((u32)TEMP_REG) << 15) |
                (((u32)TEMP_REG) << 7) | 0x13,
            &i_bytes[4]
        );
    }
    serialize32le(store_to_byte(reg), &i_bytes[sz - 4]);
    return append_obj(dst_buf, i_bytes, sz);
}

static bool zero_byte(u8 reg, sized_buf *dst_buf) {
    u32 instr = 0x23 | (((u32)reg) << 15);
    u8 i_bytes[4];
    serialize32le(instr, i_bytes);
    return append_obj(dst_buf, i_bytes, 4);
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

static const arch_sc_nums SC_NUMS = {
    .read = 63,
    .write = 64,
    .exit = 93,
};

static const arch_registers REGS = {
    .sc_num = 17 /* a7 */,
    .arg1 = 10 /* a0 */,
    .arg2 = 11 /* a1 */,
    .arg3 = 12 /* a2 */,
    .bf_ptr = 8 /* s0 */,
};

const arch_inter RISCV64_INTER = {
    .FUNCS = &FUNCS,
    .SC_NUMS = &SC_NUMS,
    .REGS = &REGS,
    /* EF_RISCV_RVC | EF_RISCV_FLOAT_ABI_DOUBLE (chosen to match Debian) */
    .FLAGS = 5,
    .ELF_ARCH = EM_RISCV,
    .ELF_DATA = ELFDATA2LSB,
};

#endif /* BFC_TARGET_RISCV64 */
