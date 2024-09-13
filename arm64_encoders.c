/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * This file provides the arch_inter for the ARM64 architecture. */
/* POSIX */
#include <unistd.h> /* size_t, off_t, write */
/* internal */
#include "arch_inter.h" /* arch_{registers, sc_nums, funcs, inter} */
#include "compat/elf.h" /* EM_X86_64, ELFDATA2LSB */
#include "err.h" /* basic_err */
#include "serialize.h" /* serialize* */
#include "types.h" /* uint*_t, int*_t, bool, UINT64_C */
#include "util.h" /* write_obj */

typedef enum {
    A64_SL_NO_SHIFT = 0x0,
    A64_SL_SHIFT16 = 0x20,
    A64_SL_SHIFT32 = 0x40,
    A64_SL_SHIFT48 = 0x60
} shift_lvl;

typedef enum {
    A64_OP_ADD = 0x91,
    A64_OP_SUB = 0xd1
} arith_op;

typedef struct {
    uint16_t imm;
    shift_lvl shift;
} shifted_imm;

typedef enum {
    A64_MT_KEEP = 0xf2,
    A64_MT_ZERO = 0xd2,
    A64_MT_INVERT = 0x92
} mov_type;

static void arm64_inject_reg_operands(uint8_t rt, uint8_t rn, uint8_t dst[4]) {
    dst[0] |= (rt | rn << 5);
    dst[1] |= (rn >> 3);
}

static void arm64_inject_imm16_operands(
    uint16_t imm,
    shift_lvl shift,
    uint8_t reg,
    uint8_t *dst
) {
    dst[0] |= (reg | ((imm & 07) << 5));
    dst[1] |= (imm >> 3);
    dst[2] |= (shift | (imm >> 11));
}

static void arm64_store_to_byte(uint8_t reg, uint8_t aux, uint8_t dst[4]) {
    dst[0] = 0x00;
    dst[1] = 0x04;
    dst[2] = 0x00;
    dst[3] = 0x38;
    arm64_inject_reg_operands(aux, reg, dst);
}

static void arm64_load_from_byte(uint8_t reg, uint8_t aux, uint8_t dst[4]) {
    dst[0] = 0x00;
    dst[1] = 0x04;
    dst[2] = 0x40;
    dst[3] = 0x38;
    arm64_inject_reg_operands(aux, reg, dst);
}

static inline uint8_t aux_reg(uint8_t reg) {
    /* return an auxiliary scratch register that isn't the same as reg to use */
    return (reg == 17) ? 16 : 17;
}

static void arm64_mov(
    mov_type mt, uint16_t imm, shift_lvl shift, uint8_t reg, uint8_t *dst
) {
    uint16_t imm_bits = (mt == A64_MT_INVERT) ? ~imm : imm;
    dst[0] = 0x00;
    dst[1] = 0x00;
    dst[2] = 0x80;
    dst[3] = mt;
    arm64_inject_imm16_operands(imm_bits, shift, reg, dst);
}

bool arm64_set_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    /* split the immediate into 4 16-bit parts - high, medium-high, medium-low,
     * and low. */
    shifted_imm parts[4] = {
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

    int i;
    for(i = 0; i < 4; i++) {
        if (parts[i].imm != default_val) break;
    }
    uint8_t instr_bytes[4] = {0, 0, 0, 0};
    if (i == 4) {
        /* all are the default value, so use this fallback instruction */
        /* (MOVZ or MOVN) reg, default_val */
        arm64_mov(lead_mt, default_val, A64_SL_NO_SHIFT, reg, instr_bytes);
        *sz += 4;
        if (!write_obj(fd, &instr_bytes, 4)) return false;
    } else {
        /* at least one needs to be set */
        /* (MOVZ or MOVN) reg, lead_imm << lead_shift */
        arm64_mov(lead_mt, parts[i].imm, parts[i].shift, reg, instr_bytes);
        *sz += 4;
        if (!write_obj(fd, &instr_bytes, 4)) return false;
        for(++i; i < 4; i++) if (parts[i].imm != default_val) {
            /*  MOVK reg, imm16 << shift */
            arm64_mov(
                A64_MT_KEEP,
                parts[i].imm,
                parts[i].shift,
                reg,
                instr_bytes
            );
            *sz += 4;
            if (!write_obj(fd, &instr_bytes, 4)) return false;
        }
    }
    return true;
}

bool arm64_reg_copy(uint8_t dst, uint8_t src, int fd, off_t *sz) {
    *sz += 4;
    /* MOV dst, src
     * technically an allias for ORR dst, XZR, src */
    return write_obj(fd, (uint8_t[]){0xe0 | dst, 0x01, src, 0xaa}, 4);
}

bool arm64_syscall(int fd, off_t *sz) {
    *sz += 4;
    return write_obj(fd, (uint8_t[]){0x01, 0x00, 0x00, 0xd4}, 4);
}

bool arm64_nop_loop_open(int fd, off_t *sz) {
    *sz += 12;
    uint8_t to_write[12] = {
        0x1f, 0x20, 0x03, 0xd5, /* NOP */
        0x1f, 0x20, 0x03, 0xd5, /* NOP */
        0x1f, 0x20, 0x03, 0xd5 /* NOP */
    };
    return write_obj(fd, to_write, 12);
}

static bool arm64_branch_cond(
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
        /* after arm64_inject_reg_operands, will be LDRB aux, [reg] */
        0x00, 0x04, 0x40, 0x38,
        /* TST reg, 0xff */
        0x1f | aux << 5, (aux >> 3) | 0x1c, 0x40, 0xf2,
        /* B.cond offset */
        cond | (offset_value << 5), offset_value >> 3, offset_value >> 11, 0x54
    };
    arm64_inject_reg_operands(aux, reg, test_and_branch);
    *sz += 12;
    return write_obj(fd, test_and_branch, 12);
}

bool arm64_jump_not_zero(uint8_t reg, int64_t offset, int fd, off_t *sz) {
    /* 1 is the not zero / not equal condition code */
    return arm64_branch_cond(reg, offset, fd, sz, 1);
}

bool arm64_jump_zero(uint8_t reg, int64_t offset, int fd, off_t *sz) {
    /* 0 is the zero / equal condition code */
    return arm64_branch_cond(reg, offset, fd, sz, 0);
}

static bool arm64_add_sub_imm(
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
    *sz += 4;
    return write_obj(fd, &instr_bytes, 4);
}

static bool arm64_add_sub(
    uint8_t reg, arith_op op, uint64_t imm, int fd, off_t *sz
) {
    if (imm < UINT64_C(0x1000)) {
        return arm64_add_sub_imm(reg, imm, false, op, fd, sz);
    } else if (imm < UINT64_C(0x1000000)) {
        bool ret = arm64_add_sub_imm(reg, imm & 0xfff000, true, op, fd, sz);
        if (ret && ((imm & 0xfff) != 0)) {
            ret &= arm64_add_sub_imm(reg, imm & 0xfff, false, op, fd, sz);
        }
        return ret;
    } else if (imm < UINT64_C(0x7fffffffffffffff)) {
        /* different byte values are needed than normal here */
        uint8_t op_byte = (op == A64_OP_ADD) ? 0x8b : 0xcb;
        uint8_t aux = aux_reg(reg);
        /* set the auxiliary register to the target value */
        if (!arm64_set_reg(aux, (int64_t)imm, fd, sz)) return false;
        /* either ADD reg, reg, aux or SUB reg, reg, aux */
        uint8_t instr_bytes[4] = { 0, 0, aux, op_byte };
        arm64_inject_reg_operands(reg, reg, instr_bytes);
        return write_obj(fd, &instr_bytes, 4);
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

bool arm64_add_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    return arm64_add_sub(reg,  A64_OP_ADD, imm, fd, sz);
}

bool arm64_sub_reg(uint8_t reg, int64_t imm, int fd, off_t *sz) {
    return arm64_add_sub(reg, A64_OP_SUB, imm, fd, sz);
}

bool arm64_inc_reg(uint8_t reg, int fd, off_t *sz) {
    return arm64_add_sub(reg, A64_OP_ADD, 1, fd, sz);
}

bool arm64_dec_reg(uint8_t reg, int fd, off_t *sz) {
    return arm64_add_sub(reg, A64_OP_SUB, 1, fd, sz);
}

static bool arm64_inc_dec_byte(
    uint8_t reg, int fd, off_t *sz,
    bool (*inner_fn)(uint8_t reg, int fd, off_t *sz)
) {

    uint8_t instr_bytes[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t aux = aux_reg(reg);
    arm64_load_from_byte(reg, aux, instr_bytes);
    *sz += 4;
    if (!write_obj(fd, &instr_bytes, 4)) return false;
    if (!inner_fn(aux, fd, sz)) return false;
    arm64_store_to_byte(reg, aux, instr_bytes);
    *sz += 4;
    return write_obj(fd, &instr_bytes, 4);
}

bool arm64_inc_byte(uint8_t reg, int fd, off_t *sz) {
    return arm64_inc_dec_byte(reg, fd, sz, &arm64_inc_reg);
}

bool arm64_dec_byte(uint8_t reg, int fd, off_t *sz) {
    return arm64_inc_dec_byte(reg, fd, sz, &arm64_dec_reg);
}

static bool arm64_add_sub_byte(
    uint8_t reg, int8_t imm8, arith_op op, int fd, off_t *sz
) {
    uint8_t imm = imm8;
    uint8_t aux = aux_reg(reg);
    uint8_t instr_bytes[12] = {
        0x00, 0x00, 0x00, 0x00,
        /* set middle instruction to ADD aux, aux, imm or SUB aux, aux, imm */
        aux | (aux << 5), (imm << 2) | (aux >> 3), imm >> 6, op,
        0x00, 0x00, 0x00, 0x00
    };
    arm64_load_from_byte(reg, aux, instr_bytes);
    /* write the lowest byte in aux back to the address stored in reg */
    arm64_store_to_byte(reg, aux, &(instr_bytes[8]));
    *sz += 12;
    return write_obj(fd, &instr_bytes, 12);
}

bool arm64_add_byte(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    return arm64_add_sub_byte(reg, imm8, A64_OP_ADD, fd, sz);
}

bool arm64_sub_byte(uint8_t reg, int8_t imm8, int fd, off_t *sz) {
    return arm64_add_sub_byte(reg, imm8, A64_OP_SUB, fd, sz);
}

bool arm64_zero_byte(uint8_t reg, int fd, off_t *sz) {
    uint8_t aux = aux_reg(reg);
    if (!arm64_set_reg(aux, 0, fd, sz)) return false;
    *sz += 4;
    uint8_t instr_bytes[4];
    arm64_store_to_byte(reg, aux, instr_bytes);
    return write_obj(fd, &instr_bytes, 4);
}

const arch_funcs ARM64_FUNCS = {
    arm64_set_reg,
    arm64_reg_copy,
    arm64_syscall,
    arm64_nop_loop_open,
    arm64_jump_zero,
    arm64_jump_not_zero,
    arm64_inc_reg,
    arm64_dec_reg,
    arm64_inc_byte,
    arm64_dec_byte,
    arm64_add_reg,
    arm64_sub_reg,
    arm64_add_byte,
    arm64_sub_byte,
    arm64_zero_byte
};

const arch_sc_nums ARM64_SC_NUMS = {
    63 /* read */,
    64 /* write */,
    93 /* exit */
};

const arch_registers ARM64_REGS = {
    8 /* sc_num = w8 */,
    0 /* arg1 = x0 */,
    1 /* arg2 = x1 */,
    2 /* arg3 = x2 */,
    19 /* bf_ptr = x19 */
};

const arch_inter ARM64_INTER = {
    &ARM64_FUNCS,
    &ARM64_SC_NUMS,
    &ARM64_REGS,
    0 /* no flags are defined for this architecture */,
    EM_AARCH64,
    ELFDATA2LSB
};
