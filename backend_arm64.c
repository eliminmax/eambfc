/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * This file provides the arch_inter for the ARM64 architecture.
 *
 * Unlike the x86_64 backend, this is based on the Rust implementation, rather
 * than the other way around. */
/* internal */
#include "arch_inter.h" /* arch_{registers, sc_nums, funcs, inter} */
#include "compat/elf.h" /* EM_X86_64, ELFDATA2LSB */
#include "config.h" /* EAMBFC_TARGET_ARM64 */
#include "err.h" /* basic_err */
#include "types.h" /* [iu]{8,16,32,64}, bool, off_t, size_t, UINT64_C */
#include "util.h" /* append_obj */
#if EAMBFC_TARGET_ARM64

/* mark a series of bytes within a u8 array as being a single instruction,
 * mostly to prevent automated code formatting from splitting them up */
#define INSTRUCTION(...) __VA_ARGS__

/* padding bytes to be replaced with an instruction within a u8 array. */
#define PAD_INSTRUCTION 0x00, 0x00, 0x00, 0x00

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
    A64_MT_KEEP = 0xf2,
    A64_MT_ZERO = 0xd2,
    A64_MT_INVERT = 0x92
} mov_type;

/* For an instruction that takes 2 registers, OR their bit values into the
 * appropriate parts of the machine code bytes in dst. */
static void inject_reg_operands(u8 rt, u8 rn, u8 dst[4]) {
    dst[0] |= (rt | rn << 5);
    dst[1] |= (rn >> 3);
}

/* OR the immediate bytes for imm, shift, and x.reg into dst.
 *
 * This assumes that dst has everything except the register, shift, and
 * immediate bits set, and that the immediate bits are set to zero. */
static void inject_imm16_operands(u16 imm, shift_lvl shift, u8 reg, u8 *dst) {
    dst[0] |= (reg | ((imm & 07) << 5));
    dst[1] |= (imm >> 3);
    dst[2] |= (shift | (imm >> 11));
}

/* set dst to the machine code for STRB w.aux, x.reg */
static void store_to_byte(u8 reg, u8 aux, u8 dst[4]) {
    dst[0] = 0x00;
    dst[1] = 0x04;
    dst[2] = 0x00;
    dst[3] = 0x38;
    inject_reg_operands(aux, reg, dst);
}

/* set dst to the machine code for LDRB w.aux, x.reg */
static void load_from_byte(u8 reg, u8 aux, u8 dst[4]) {
    dst[0] = 0x00;
    dst[1] = 0x04;
    dst[2] = 0x40;
    dst[3] = 0x38;
    inject_reg_operands(aux, reg, dst);
}

/* return an scratch register that isn't the same as x.reg to use */
static u8 aux_reg(u8 reg) {
    return (reg == 17) ? 16 : 17;
}

/* set dst to the machine code for one of MOVK, MOVN, or MOVZ, depending on mt,
 * with the given operands. */
static void mov(mov_type mt, u16 imm, shift_lvl shift, u8 reg, u8 *dst) {
    /* for MOVN, the bits need to be inverted. Ask Arm, not me. */
    u16 imm_bits = (mt == A64_MT_INVERT) ? ~imm : imm;
    dst[0] = 0x00;
    dst[1] = 0x00;
    dst[2] = 0x80;
    dst[3] = mt;
    inject_imm16_operands(imm_bits, shift, reg, dst);
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
    for (i = 0; i < 4; i++)
        if (parts[i].imm16 != default_val) break;
    u8 instr_bytes[4] = {0, 0, 0, 0};
    /* check if the end was reached without finding a non-default value */
    if (i == 4) {
        /* all are the default value, so use this fallback instruction */
        /* (MOVZ|MOVN) x.reg, default_val */
        mov(lead_mt, default_val, A64_SL_NO_SHIFT, reg, instr_bytes);
        if (!append_obj(dst_buf, &instr_bytes, 4)) return false;
    } else {
        /* at least one needs to be set */
        /* (MOVZ|MOVN) x.reg, lead_imm{, lsl lead_shift} */
        mov(lead_mt, parts[i].imm16, parts[i].shift, reg, instr_bytes);
        if (!append_obj(dst_buf, &instr_bytes, 4)) return false;
        for (++i; i < 4; i++)
            if (parts[i].imm16 != default_val) {
                /*  MOVK x[reg], imm16{, lsl shift} */
                mov(A64_MT_KEEP,
                    parts[i].imm16,
                    parts[i].shift,
                    reg,
                    instr_bytes);
                if (!append_obj(dst_buf, &instr_bytes, 4)) return false;
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

/* NOP; NOP; NOP */
#define NOP 0x1f, 0x20, 0x03, 0xdf

static bool nop_loop_open(sized_buf *dst_buf) {
    u8 instr_bytes[12] = {NOP, NOP, NOP};
    return append_obj(dst_buf, &instr_bytes, 12);
}

/* LDRB w.aux, x.reg; TST w.aux, 0xff; B.cond offset */
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
    if ((offset < -0x200000 || offset > 0x1fffff)) {
        basic_err(
            "JUMP_TOO_LONG",
            "offset is outside the range of possible 21-bit signed values"
        );
        return false;
    }
    u32 off_val = 1 + ((((u32)offset) >> 2) & 0x7fffff);
    u8 aux = aux_reg(reg);
    u8 test_and_branch[12] = {
        /* after inject_reg_operands, will be LDRB w.aux, x.reg */
        PAD_INSTRUCTION,
        /* TST x.reg, 0xff */
        INSTRUCTION(0x1f | aux << 5, (aux >> 3) | 0x1c, 0x40, 0xf2),
        /* B.cond offset */
        INSTRUCTION(cond | (off_val << 5), off_val >> 3, off_val >> 11, 0x54),
    };
    load_from_byte(reg, aux, test_and_branch);
    return append_obj(dst_buf, test_and_branch, 12);
}

/* LDRB w.aux, x.reg; TST w.aux, 0xff; B.NE offset */
static bool jump_not_zero(u8 reg, i64 offset, sized_buf *dst_buf) {
    /* 1 is the not zero / not equal condition code */
    return branch_cond(reg, offset, dst_buf, 1);
}

/* LDRB w.aux, x.reg; TST w.aux, 0xff; B.E offset */
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
    /* if shift is set to true, imm needs to be shifted back by 12 bits */
    u16 imm_bits = shift ? (imm >> 12) : imm;
    /* (ADD|SUB) x.reg, x.reg, imm{, lsl12} */
    u8 instr_bytes[4] = {
        reg | (reg << 5),
        (reg >> 3) | ((imm_bits << 2) & 0xff),
        (imm_bits >> 6) | (shift ? 0x40 : 0x0),
        op,
    };
    return append_obj(dst_buf, &instr_bytes, 4);
}

static bool add_sub(u8 reg, arith_op op, u64 imm, sized_buf *dst_buf) {
    /* If the immediate fits within 12 bits, it's a far simpler process - simply
     * ADD or SUB the immediate. If it fits within 24 bits, use an ADD or SUB,
     * and shift the higher 12 of the 24 bits. If the 12 lower bits are
     * non-zero, then also ADD or SUB them afterwards.
     *
     * If the immediate does not fit within the lower 24 bits, then first set an
     * auxiliary register to the immediate, then ADD or SUB that, assuming that
     * it still fits within the 64-bit signed limit. */
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
        u8 aux = aux_reg(reg);
        /* set register x.aux to the target value */
        if (!set_reg(aux, (i64)imm, dst_buf)) return false;
        /* either ADD x.reg, x.reg, x.aux or SUB x.reg, x.reg, x.aux */
        u8 instr_bytes[4] = {0, 0, aux, op_byte};
        inject_reg_operands(reg, reg, instr_bytes);
        return append_obj(dst_buf, &instr_bytes, 4);
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

/* to increment or decrement a byte, first load it into an auxiliary register,
 * then call an inner function (either inc_reg or dec_reg) on that register,
 * and finally, store the least significant byte of the register into that
 * memory address. */
static bool inc_dec_byte(
    u8 reg, sized_buf *dst_buf, bool (*inner_fn)(u8 reg, sized_buf *dst_buf)
) {
    u8 instr_bytes[4] = {0x00, 0x00, 0x00, 0x00};
    u8 aux = aux_reg(reg);
    load_from_byte(reg, aux, instr_bytes);
    if (!append_obj(dst_buf, &instr_bytes, 4)) return false;
    if (!inner_fn(aux, dst_buf)) return false;
    store_to_byte(reg, aux, instr_bytes);
    return append_obj(dst_buf, &instr_bytes, 4);
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
    u8 aux = aux_reg(reg);
    u8 instr_bytes[12] = {
        PAD_INSTRUCTION,
        /* set middle instruction to (ADD|SUB) x.aux, x.aux, imm */
        INSTRUCTION(aux | (aux << 5), (imm8 << 2) | (aux >> 3), imm8 >> 6, op),
        PAD_INSTRUCTION,
    };
    /* load the byte in address stored in x.reg into x.aux */
    load_from_byte(reg, aux, instr_bytes);
    /* store the lowest byte in x.aux back to the address in x.reg */
    store_to_byte(reg, aux, &(instr_bytes[8]));
    return append_obj(dst_buf, &instr_bytes, 12);
}

/* now, the last few thin wrapper functions */

static bool add_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    return add_sub_byte(reg, imm8, A64_OP_ADD, dst_buf);
}

static bool sub_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    return add_sub_byte(reg, imm8, A64_OP_SUB, dst_buf);
}

/* a function to zero out a memory address. It sets an auxiliary register to 0,
 * then stores its least significant byte in the address in reg. */
static bool zero_byte(u8 reg, sized_buf *dst_buf) {
    u8 aux = aux_reg(reg);
    if (!set_reg(aux, 0, dst_buf)) return false;
    u8 instr_bytes[4];
    store_to_byte(reg, aux, instr_bytes);
    return append_obj(dst_buf, &instr_bytes, 4);
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
#endif /* EAMBFC_TARGEET_ARM64 */
