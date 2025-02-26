/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * This file provides the arch_inter for the ARM64 architecture.
 *
 * Unlike the x86_64 backend, this is based on the Rust implementation, rather
 * than the other way around. */
/* internal */
#include "arch_inter.h"
#include "compat/elf.h"
#include "config.h"
#include "err.h"
#include "serialize.h"
#include "types.h"
#include "util.h"
#if BFC_TARGET_ARM64

/* in MOVK, MOVZ, and MOVN instructions, these correspond to the bits used
 * within the 3rd byte to indicate shift level. */
typedef enum {
    A64_SL_NO_SHIFT = 0x0,
    A64_SL_SHIFT16 = 0x20,
    A64_SL_SHIFT32 = 0x40,
    A64_SL_SHIFT48 = 0x60
} shift_lvl;

/* these byte values often appear within ADD and SUB instructions, and are the
 * only difference between them. */
typedef enum { A64_OP_ADD = 0x91, A64_OP_SUB = 0xd1 } arith_op;

/* the final byte of each of the MOVK, MOVN, and MOVZ instructions are used as
 * the corresponding enum values here */
typedef enum {
    A64_MT_KEEP = 0x3,
    A64_MT_ZERO = 0x2,
    A64_MT_INVERT = 0x0
} mov_type;

/* set dst to the machine code for STRB w17, x.reg */
static void store_to_byte(u8 src, u8 *dst) {
    serialize32le(0x38000411 | (((u32)src) << 5), dst);
}

/* set dst to the machine code for LDRB w17, x.reg */
static void load_from_byte(u8 reg, u8 dst[4]) {
    serialize32le(0x38400411 | (((u32)reg) << 5), dst);
}

const u8 TEMP_REG = 17;

/* set dst to the machine code for one of MOVK, MOVN, or MOVZ, depending on mt,
 * with the given operands. */
static void mov(mov_type mt, u16 imm, shift_lvl shift, u8 reg, u8 *dst) {
    /* for MOVN, the bits need to be inverted. Ask Arm, not me. */
    if (mt == A64_MT_INVERT) imm = ~imm;
    u32 instr = 0x92800000 | ((u32)mt << 29) | ((u32)shift << 16) |
                ((u32)imm << 5) | reg;
    serialize32le(instr, dst);
}

/* Choose a combination of MOVZ, MOVK, and MOVN that sets register x.reg to
 * the immediate imm */
static bool set_reg(u8 reg, i64 imm, sized_buf *dst_buf) {
    u16 default_val;
    mov_type lead_mt;

    /* split the immediate into 4 16-bit parts - high, medium-high, medium-low,
     * and low. */
    struct shifted_imm {
        u16 imm16;
        shift_lvl shift;
    };
    const struct shifted_imm parts[4] = {
        {(u16)imm, A64_SL_NO_SHIFT},
        {(u16)(imm >> 16), A64_SL_SHIFT16},
        {(u16)(imm >> 32), A64_SL_SHIFT32},
        {(u16)(imm >> 48), A64_SL_SHIFT48},
    };
    if (imm < 0) {
        default_val = 0xffff;
        lead_mt = A64_MT_INVERT;
    } else {
        default_val = 0;
        lead_mt = A64_MT_ZERO;
    }
    /* skip to the first part with non-default imm16 values. */
    int i;
    for (i = 0; i < 4; i++) {
        if (parts[i].imm16 != default_val) break;
    }
    u8 *instr_bytes = sb_reserve(dst_buf, 4);
    /* check if the end was reached without finding a non-default value */
    if (i == 4) {
        /* all are the default value, so use this fallback instruction */
        /* (MOVZ|MOVN) x.reg, default_val */
        mov(lead_mt, default_val, A64_SL_NO_SHIFT, reg, instr_bytes);
    } else {
        /* at least one needs to be set */
        /* (MOVZ|MOVN) x.reg, lead_imm{, lsl lead_shift} */
        mov(lead_mt, parts[i].imm16, parts[i].shift, reg, instr_bytes);
        for (++i; i < 4; i++)
            if (parts[i].imm16 != default_val) {
                /*  MOVK x[reg], imm16{, lsl shift} */
                mov(A64_MT_KEEP,
                    parts[i].imm16,
                    parts[i].shift,
                    reg,
                    sb_reserve(dst_buf, 4));
            }
    }
    return true;
}

/* MOV x.dst, x.src
 * technically an alias for ORR x.dst, XZR, x.src */
static bool reg_copy(u8 dst, u8 src, sized_buf *dst_buf) {
    return append_obj(dst_buf, (u8[]){0xe0 | dst, 0x03, src, 0xaa}, 4);
}

/* SVC 0 */
static bool syscall(sized_buf *dst_buf) {
    return append_obj(dst_buf, (u8[]){0x01, 0x00, 0x00, 0xd4}, 4);
}

static bool nop_loop_open(sized_buf *dst_buf) {
#define NOP 0x1f, 0x20, 0x03, 0xdf
    return append_obj(dst_buf, (u8[]){NOP, NOP, NOP}, 12);
#undef NOP
}

/* LDRB w17, x.reg; TST w17, 0xff; B.cond offset */
static bool branch_cond(u8 reg, i64 offset, sized_buf *dst_buf, u8 cond) {
    if ((offset % 4) != 0) {
        basic_err(
            "INVALID_JUMP_ADDRESS",
            "offset is an invalid address offset (offset % 4 != 0)"
        );
        return false;
    }
    /* use some bit shifts to check if the value is in range
     * (19 immediate bits are used, but as it must be a multiple of 4, it treats
     * them as though they're followed by an implicit 0b00, so it needs to fit
     * within the range of possible 21-bit 2's complement values. */
    if (!bit_fits(offset, 21)) {
        basic_err(
            "JUMP_TOO_LONG",
            "offset is outside the range of possible 21-bit signed values"
        );
        return false;
    }
    u32 off_val = ((offset >> 2) + 1) & 0x7ffff;
    u8 *test_and_branch = sb_reserve(dst_buf, 12);
    /* LDRB w17, x.reg */
    load_from_byte(reg, test_and_branch);
    /* TST x17, 0xff */
    serialize32le(0xf2401e3f, &test_and_branch[4]);
    /* B.cond offset */
    serialize32le(0x54000000 | cond | (off_val << 5), &test_and_branch[8]);
    return true;
}

/* LDRB w17, x.reg; TST w17, 0xff; B.NE offset */
static bool jump_not_zero(u8 reg, i64 offset, sized_buf *dst_buf) {
    /* 1 is the not zero / not equal condition code */
    return branch_cond(reg, offset, dst_buf, 1);
}

/* LDRB w17, x.reg; TST w17, 0xff; B.E offset */
static bool jump_zero(u8 reg, i64 offset, sized_buf *dst_buf) {
    /* 0 is the zero / equal condition code */
    return branch_cond(reg, offset, dst_buf, 0);
}

static bool add_sub_imm(
    u8 reg, u64 imm, bool shift, arith_op op, sized_buf *dst_buf
) {
    /* The immediate can be a 12-bit immediate or a 24-bit immediate with the
     * lower 12 bits set to zero, in which case shift should be set to true. */
    if ((shift && (imm & ~0xfff000) != 0) || (!shift && (imm & ~0xfff) != 0)) {
        basic_err("IMMEDIATE_TOO_LARGE", "value is invalid for shift level.");
        return false;
    }
    /* align the immediate bits */
    u32 aligned = shift ? (imm >> 2) : (imm << 10);
    /* (ADD|SUB) x.reg, x.reg, imm{, lsl12} */
    serialize32le(
        (op << 24) | aligned | (shift ? (1 << 22) : 0) | (reg << 5) | reg,
        sb_reserve(dst_buf, 4)
    );

    return true;
}

static bool add_sub(u8 reg, arith_op op, u64 imm, sized_buf *dst_buf) {
    /* If the immediate fits within 12 bits, it's a far simpler process - simply
     * ADD or SUB the immediate. If it fits within 24 bits, use an ADD or SUB,
     * and shift the higher 12 of the 24 bits. If the 12 lower bits are
     * non-zero, then also ADD or SUB them afterwards.
     *
     * If the immediate does not fit within the lower 24 bits, then first set
     * x17 to the immediate, then ADD or SUB it. */
    if (imm < UINT64_C(0x1000)) {
        return add_sub_imm(reg, imm, false, op, dst_buf);
    } else if (imm < UINT64_C(0x1000000)) {
        bool ret = add_sub_imm(reg, imm & 0xfff000, true, op, dst_buf);
        if (ret && ((imm & 0xfff) != 0)) {
            ret &= add_sub_imm(reg, imm & 0xfff, false, op, dst_buf);
        }
        return ret;
    } else if (imm < UINT64_C(0x7fffffffffffffff)) {
        /* different byte values are needed than normal here */
        u8 op_byte = (op == A64_OP_ADD) ? 0x8b : 0xcb;
        /* set register x17 to the target value */
        if (!set_reg(TEMP_REG, (i64)imm, dst_buf)) return false;
        /* either ADD x.reg, x.reg, x17 or SUB x.reg, x.reg, x17 */
        serialize32le(
            op_byte << 24 | TEMP_REG << 16 | reg | reg << 5,
            sb_reserve(dst_buf, 4)
        );
        return true;
    }
    /* over the 64-bit signed int limit, so print an error and return false. */
    const char err_char_str[2] = {(op == A64_OP_ADD) ? '>' : '<', '\0'};
    param_err(
        "TOO_MANY_INSTRUCTIONS",
        "Over 8192 PiB of consecutive `{}` instructions",
        err_char_str
    );
    return false;
}

/* add_reg, sub_reg, inc_reg, and dec_reg are all simple wrappers around
 * add_sub. */
static bool add_reg(u8 reg, u64 imm, sized_buf *dst_buf) {
    return add_sub(reg, A64_OP_ADD, imm, dst_buf);
}

static bool sub_reg(u8 reg, u64 imm, sized_buf *dst_buf) {
    return add_sub(reg, A64_OP_SUB, imm, dst_buf);
}

static bool inc_reg(u8 reg, sized_buf *dst_buf) {
    return add_sub(reg, A64_OP_ADD, 1, dst_buf);
}

static bool dec_reg(u8 reg, sized_buf *dst_buf) {
    return add_sub(reg, A64_OP_SUB, 1, dst_buf);
}

/* to increment or decrement a byte, first load it into TEMP_REG,
 * then call an inner function (either inc_reg or dec_reg) on TEMP_REG,
 * and finally, store the least significant byte of TEMP_REG into the byte */
typedef bool (*inc_dec_fn)(u8 reg, sized_buf *dst_buf);

static bool inc_dec_byte(u8 reg, sized_buf *dst_buf, inc_dec_fn inner_fn) {
    load_from_byte(reg, sb_reserve(dst_buf, 4));
    if (!inner_fn(TEMP_REG, dst_buf)) return false;
    store_to_byte(reg, sb_reserve(dst_buf, 4));
    return true;
}

/* next, some thin wrappers around the inc_dec_byte function that can be used in
 * the arch_funcs struct */
static bool inc_byte(u8 reg, sized_buf *dst_buf) {
    return inc_dec_byte(reg, dst_buf, &inc_reg);
}

static bool dec_byte(u8 reg, sized_buf *dst_buf) {
    return inc_dec_byte(reg, dst_buf, &dec_reg);
}

/* similar to add_sub_reg, but operating on an auxiliary register, after loading
 * from byte and before restoring to that byte, much like inc_dec_byte */
static bool add_sub_byte(u8 reg, u8 imm8, arith_op op, sized_buf *dst_buf) {
    u8 *i_bytes = sb_reserve(dst_buf, 12);
    /* load the byte in address stored in x.reg into x17 */
    load_from_byte(reg, i_bytes);
    /* (ADD|SUB) x.17, x.17, imm8 */
    serialize32le((op << 24) | (imm8 << 10) | 0x231, &i_bytes[4]);
    /* store the lowest byte in x17 back to the address in x.reg */
    store_to_byte(reg, &i_bytes[8]);
    return true;
}

/* a function to zero out a memory address. */
static bool zero_byte(u8 reg, sized_buf *dst_buf) {
    serialize32le(0x3800041f | reg << 5, sb_reserve(dst_buf, 4));
    return true;
}

/* now, the last few thin wrapper functions */

static bool add_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    return add_sub_byte(reg, imm8, A64_OP_ADD, dst_buf);
}

static bool sub_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    return add_sub_byte(reg, imm8, A64_OP_SUB, dst_buf);
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
    .sc_num = 8 /* w8 */,
    .arg1 = 0 /* x0 */,
    .arg2 = 1 /* x1 */,
    .arg3 = 2 /* x2 */,
    .bf_ptr = 19 /* x19 */,
};

const arch_inter ARM64_INTER = {
    .FUNCS = &FUNCS,
    .SC_NUMS = &SC_NUMS,
    .REGS = &REGS,
    .FLAGS = 0 /* no flags are defined for this architecture */,
    .ELF_ARCH = EM_AARCH64,
    .ELF_DATA = ELFDATA2LSB,
};
#endif /* BFC_TARGET_ARM64 */
