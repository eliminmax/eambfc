/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * The implementation for IBM z/Architecture systems. Not actually expected to
 * run on any IBM mainframes, but I wanted to be sure that big-endian systems
 * are properly supported. */

/* internal */
#include "arch_inter.h" /* arch_{registers, sc_nums, funcs, inter}  */
#include "compat/elf.h" /* EM_S390, ELFDATA2MSB */
#include "err.h" /* basic_err */
#include "types.h" /* uint*_t, int*_t, bool, off_t, size_t, UINT64_C */
#include "util.h" /* append_obj */

static bool set_reg(uint8_t reg, int64_t imm, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool reg_copy(uint8_t dst, uint8_t src, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool syscall(sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool nop_loop_open(sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool jump_zero(uint8_t reg, int64_t offset, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool jump_not_zero(
    uint8_t reg, int64_t offset, sized_buf *dst_buf
);

static bool inc_reg(uint8_t reg, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool dec_reg(uint8_t reg, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool inc_byte(uint8_t reg, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool dec_byte(uint8_t reg, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool add_reg(uint8_t reg, int64_t imm, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool sub_reg(uint8_t reg, int64_t imm, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool add_byte(uint8_t reg, int8_t imm8, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool sub_byte(uint8_t reg, int8_t imm8, sized_buf *dst_buf){
    /* TODO */
    return false;
}

static bool zero_byte(uint8_t reg, sized_buf *dst_buf){
    /* TODO */
    return false;
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
    0 /* TODO read */,
    0 /* TODO write */,
    0 /* TODO exit */
};

static const arch_registers REGS = {
    0 /* sc_num = TODO */,
    0 /* arg1 = TODO */,
    0 /* arg2 = TODO */,
    0 /* arg3 = TODO */,
    0 /* bf_ptr = TODO */
};


const arch_inter ARM64_INTER = {
    &FUNCS,
    &SC_NUMS,
    &REGS,
    0 /* no flags are defined for this architecture */,
    EM_S390,
    ELFDATA2MSB
};

