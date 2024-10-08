/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * This file provides the arch_inter for the ARM64 architecture.
 *
 * Unlike the x86_64 backend, this is based on the Rust implementation, rather
 * than the other way around. */

/* POSIX */
#include <unistd.h> /* write */
/* internal */
#include "arch_inter.h" /* arch_{registers, sc_nums, funcs, inter} */
#include "compat/elf.h" /* EM_X86_64, ELFDATA2LSB */
#include "err.h" /* basic_err */
#include "types.h" /* uint*_t, int*_t, bool, off_t, size_t UINT64_C */
#include "util.h" /* write_obj */

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
typedef enum {
    A64_OP_ADD = 0x91,
    A64_OP_SUB = 0xd1
} arith_op;

/* the final byte of each of the MOVK, MOVN, and MOVZ instructions are used as
 * the corresponding enum values here */
typedef enum {
    A64_MT_KEEP = 0xf2,
    A64_MT_ZERO = 0xd2,
    A64_MT_INVERT = 0x92
} mov_type;

/* For an instruction that takes 2 registers, OR their bit values into the
 * appropriate parts of the machine code bytes in dst. */
static void inject_reg_operands(uint8_t rt, uint8_t rn, uint8_t dst[4]) {
    dst[0] |= (rt | rn << 5);
    dst[1] |= (rn >> 3);
}

/* OR the immediate bytes for imm, shift, and x.reg into dst.
 *
 * This assumes that dst has everything except the register, shift, and
 * immediate bits set, and that the immediate bits are set to zero. */
static void inject_imm16_operands(
    uint16_t imm, shift_lvl shift, uint8_t reg, uint8_t *dst
) {
    dst[0] |= (reg | ((imm & 07) << 5));
    dst[1] |= (imm >> 3);
    dst[2] |= (shift | (imm >> 11));
}

/* set dst to the machine code for STRB w.aux, x.reg */
static void store_to_byte(uint8_t reg, uint8_t aux, uint8_t dst[4]) {
    dst[0] = 0x00;
    dst[1] = 0x04;
    dst[2] = 0x00;
    dst[3] = 0x38;
    inject_reg_operands(aux, reg, dst);
}

/* set dst to the machine code for LDRB w.aux, x.reg */
static void load_from_byte(uint8_t reg, uint8_t aux, uint8_t dst[4]) {
    dst[0] = 0x00;
    dst[1] = 0x04;
    dst[2] = 0x40;
    dst[3] = 0x38;
    inject_reg_operands(aux, reg, dst);
}

        /* return an scratch register that isn't the same as x.reg to use */
static inline uint8_t aux_reg(uint8_t reg) {
    return (reg == 17) ? 16 : 17;
}

/* set dst to the machine code for one of MOVK, MOVN, or MOVZ, depending on mt,
 * with the given operands. */
static void mov(
    mov_type mt, uint16_t imm, shift_lvl shift, uint8_t reg, uint8_t *dst
) {
    /* for MOVN, the bits need to be inverted. Ask Arm, not me. */
    uint16_t imm_bits = (mt == A64_MT_INVERT) ? ~imm : imm;
    dst[0] = 0x00;
    dst[1] = 0x00;
    dst[2] = 0x80;
    dst[3] = mt;
    inject_imm16_operands(imm_bits, shift, reg, dst);
}

/* Choose a combination of MOVZ, MOVK, and MOVN that sets register x.reg to
 * the immediate imm */
static bool set_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    /* split the immediate into 4 16-bit parts - high, medium-high, medium-low,
     * and low. */
    struct shifted_imm { uint16_t imm16; shift_lvl shift; };
    struct shifted_imm parts[4] = {
        {(uint16_t)imm, A64_SL_NO_SHIFT},
        {(uint16_t)(imm >> 16), A64_SL_SHIFT16},
        {(uint16_t)(imm >> 32), A64_SL_SHIFT32},
        {(uint16_t)(imm >> 48), A64_SL_SHIFT48}
    };
    uint16_t default_val;
    mov_type lead_mt;
    if (imm < 0) {
        default_val = 0xffff;
        lead_mt = A64_MT_INVERT;
    } else {
        default_val = 0;
        lead_mt = A64_MT_ZERO;
    }
    /* skip to the first part with non-default imm16 values. */
    int i;
    for(i = 0; i < 4; i++) if (parts[i].imm16 != default_val) break;
    uint8_t instr_bytes[4] = {0, 0, 0, 0};
    /* check if the end was reached without finding a non-default value */
    if (i == 4) {
        /* all are the default value, so use this fallback instruction */
        /* (MOVZ|MOVN) x.reg, default_val */
        mov(lead_mt, default_val, A64_SL_NO_SHIFT, reg, instr_bytes);
        if (!write_obj(fd, &instr_bytes, 4, sz)) return false;
    } else {
        /* at least one needs to be set */
        /* (MOVZ|MOVN) x.reg, lead_imm{, lsl lead_shift} */
        mov(lead_mt, parts[i].imm16, parts[i].shift, reg, instr_bytes);
        if (!write_obj(fd, &instr_bytes, 4, sz)) return false;
        for(++i; i < 4; i++) if (parts[i].imm16 != default_val) {
            /*  MOVK x[reg], imm16{, lsl shift} */
            mov(
                A64_MT_KEEP,
                parts[i].imm16,
                parts[i].shift,
                reg,
                instr_bytes
            );
            if (!write_obj(fd, &instr_bytes, 4, sz)) return false;
        }
    }
    return true;
}

/* MOV x.dst, x.src
 * technically an alias for ORR x.dst, XZR, x.src */
static bool reg_copy(uint8_t dst, uint8_t src, int fd, off_t *sz) {
    return write_obj(fd, (uint8_t[]){0xe0 | dst, 0x01, src, 0xaa}, 4, sz);
}

/* SVC 0 */
static bool syscall(int fd, off_t *sz) {
    return write_obj(fd, (uint8_t[]){0x01, 0x00, 0x00, 0xd4}, 4, sz);
}

/* NOP; NOP; NOP */
static bool nop_loop_open(int fd, off_t *sz) {
    uint8_t instr_bytes[12] = {
        0x1f, 0x20, 0x03, 0xd5, /* NOP */
        0x1f, 0x20, 0x03, 0xd5, /* NOP */
        0x1f, 0x20, 0x03, 0xd5 /* NOP */
    };
    return write_obj(fd, instr_bytes, 12, sz);
}

/* LDRB w.aux, x.reg; TST w.aux, 0xff; B.cond offset */
static bool branch_cond(
    uint8_t reg, int64_t offset, int fd, off_t *sz, uint8_t cond
) {
    if ((offset % 4) != 0) {
        basic_err(
            "INVALID_JUMP_ADDRESS",
            "offset is an invalid address offset (offset % 4 != 0)"
        );
        return false;
    }
    if ((offset > 0 && (offset >> 44) != 0) ||
        (offset < 0 && (offset >> 44) != -1)) {
        basic_err(
            "JUMP_TOO_LONG",
            "offset is outside the range of possible 20-bit signed values"
        );
        return false;
    }
    uint32_t offset_value = 1 + ((((uint32_t) offset) >> 2) & 0x7fffff);
    uint8_t aux = aux_reg(reg);
    uint8_t test_and_branch[12] = {
        /* after inject_reg_operands, will be LDRB w.aux, x.reg */
        0x00, 0x04, 0x40, 0x38,
        /* TST x.reg, 0xff */
        0x1f | aux << 5, (aux >> 3) | 0x1c, 0x40, 0xf2,
        /* B.cond offset */
        cond | (offset_value << 5), offset_value >> 3, offset_value >> 11, 0x54
    };
    inject_reg_operands(aux, reg, test_and_branch);
    return write_obj(fd, test_and_branch, 12, sz);
}

/* LDRB w.aux, x.reg; TST w.aux, 0xff; B.NE offset */
static bool jump_not_zero(uint8_t reg, int64_t offset, int fd, off_t *sz) {
    /* 1 is the not zero / not equal condition code */
    return branch_cond(reg, offset, fd, sz, 1);
}

/* LDRB w.aux, x.reg; TST w.aux, 0xff; B.E offset */
static bool jump_zero(uint8_t reg, int64_t offset, int fd, off_t *sz) {
    /* 0 is the zero / equal condition code */
    return branch_cond(reg, offset, fd, sz, 0);
}

static bool add_sub_imm(
    uint8_t reg, uint64_t imm, bool shift, arith_op op, int fd, off_t *sz
) {
    if ((shift && (imm & ~0xfff000) != 0) || (!shift && (imm & ~0xfff) != 0)) {
        basic_err("IMMEDIATE_TOO_LARGE", "value is invalid for shift level.");
        return false;
    }
    uint16_t imm_bits = shift ? (imm >> 12) : imm;
    uint8_t instr_bytes[4] = {
        reg | (reg << 5),
        (reg >> 3) | ((imm_bits << 2) & 0xff),
        (imm_bits >> 6) | (shift ? 0x40 : 0x0),
        op
    };
    return write_obj(fd, &instr_bytes, 4, sz);
}

static bool add_sub(
    uint8_t reg, arith_op op, uint64_t imm, int fd, off_t *sz
) {
    if (imm < UINT64_C(0x1000)) {
        return add_sub_imm(reg, imm, false, op, fd, sz);
    } else if (imm < UINT64_C(0x1000000)) {
        bool ret = add_sub_imm(reg, imm & 0xfff000, true, op, fd, sz);
        if (ret && ((imm & 0xfff) != 0)) {
            ret &= add_sub_imm(reg, imm & 0xfff, false, op, fd, sz);
        }
        return ret;
    } else if (imm < UINT64_C(0x7fffffffffffffff)) {
        /* different byte values are needed than normal here */
        uint8_t op_byte = (op == A64_OP_ADD) ? 0x8b : 0xcb;
        uint8_t aux = aux_reg(reg);
        /* set register x.aux to the target value */
        if (!set_reg(aux, (int64_t)imm, fd, sz)) return false;
        /* either ADD x.reg, x.reg, x.aux or SUB x.reg, x.reg, x.aux */
        uint8_t instr_bytes[4] = { 0, 0, aux, op_byte };
        inject_reg_operands(reg, reg, instr_bytes);
        return write_obj(fd, &instr_bytes, 4, sz);
    }
    /* over the 64-bit signed int limit, so print an error and return false. */
    char err_char_str[2] = {
        (op == A64_OP_ADD) ? '>' : '<',
        '\0'
    };
    param_err(
        "TOO_MANY_INSTRUCTIONS",
        "Over 8192 PiB of consecitive `{}` instructions",
        err_char_str
    );
    return false;
}

static bool add_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    return add_sub(reg,  A64_OP_ADD, imm, fd, sz);
}

static bool sub_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    return add_sub(reg, A64_OP_SUB, imm, fd, sz);
}

static bool inc_reg(uint8_t reg, int fd, off_t *sz) {
    return add_sub(reg, A64_OP_ADD, 1, fd, sz);
}

static bool dec_reg(uint8_t reg, int fd, off_t *sz) {
    return add_sub(reg, A64_OP_SUB, 1, fd, sz);
}

static bool inc_dec_byte(
    uint8_t reg, int fd, off_t *sz,
    bool (*inner_fn)(uint8_t reg, int fd, off_t *sz)
) {

    uint8_t instr_bytes[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t aux = aux_reg(reg);
    load_from_byte(reg, aux, instr_bytes);
    if (!write_obj(fd, &instr_bytes, 4, sz)) return false;
    if (!inner_fn(aux, fd, sz)) return false;
    store_to_byte(reg, aux, instr_bytes);
    return write_obj(fd, &instr_bytes, 4, sz);
}

static bool inc_byte(uint8_t reg, int fd, off_t *sz) {
    return inc_dec_byte(reg, fd, sz, &inc_reg);
}

static bool dec_byte(uint8_t reg, int fd, off_t *sz) {
    return inc_dec_byte(reg, fd, sz, &dec_reg);
}

static bool add_sub_byte(
    uint8_t reg, int8_t imm8, arith_op op, int fd, off_t *sz
) {
    uint8_t imm = imm8;
    uint8_t aux = aux_reg(reg);
    uint8_t instr_bytes[12] = {
        0x00, 0x00, 0x00, 0x00,
        /* set middle instruction to (ADD|SUB) x.aux, x.aux, imm */
        aux | (aux << 5), (imm << 2) | (aux >> 3), imm >> 6, op,
        0x00, 0x00, 0x00, 0x00
    };
    /* load the byte in address stored in x.reg into x.aux */
    load_from_byte(reg, aux, instr_bytes);
    /* store the lowest byte in x.aux back to the address in x.reg */
    store_to_byte(reg, aux, &(instr_bytes[8]));
    return write_obj(fd, &instr_bytes, 12, sz);
}

static bool add_byte(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    return add_sub_byte(reg, imm8, A64_OP_ADD, fd, sz);
}

static bool sub_byte(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    return add_sub_byte(reg, imm8, A64_OP_SUB, fd, sz);
}

static bool zero_byte(uint8_t reg, int fd, off_t *sz) {
    uint8_t aux = aux_reg(reg);
    if (!set_reg(aux, 0, fd, sz)) return false;
    uint8_t instr_bytes[4];
    store_to_byte(reg, aux, instr_bytes);
    return write_obj(fd, &instr_bytes, 4, sz);
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
    zero_byte
};

static const arch_sc_nums SC_NUMS = {
    63 /* read */,
    64 /* write */,
    93 /* exit */
};

static const arch_registers REGS = {
    8 /* sc_num = w8 */,
    0 /* arg1 = x0 */,
    1 /* arg2 = x1 */,
    2 /* arg3 = x2 */,
    19 /* bf_ptr = x19 */
};

const arch_inter ARM64_INTER = {
    &FUNCS,
    &SC_NUMS,
    &REGS,
    0 /* no flags are defined for this architecture */,
    EM_AARCH64,
    ELFDATA2LSB
};
