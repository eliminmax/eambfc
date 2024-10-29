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

/* SVC 0 */
static bool syscall(sized_buf *dst_buf){
    /* SVC 0  - second byte, if non-zero, is the system call number.
     * On s390x, if the SC number is less than 256, it it can be passed as the
     * second byte of the instruction, but taking advantage of that would
     * require an extra parameter in the arch_inter.syscall prototype, which
     * would only be useful on this specific architecture. The initial
     * implementation ofs390x must be complete and working without any change
     * outside of the designated insertion points. */
    return append_obj(dst_buf, (uint8_t[]){ 0x0a, 0x00 }, 2);
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
    3 /* read */,
    4 /* write */,
    1 /* exit */
};

static const arch_registers REGS = {
    1 /* sc_num */,
    2 /* arg1 */,
    3 /* arg2 */,
    4 /* arg3 */,
    7 /* bf_ptr */
};


const arch_inter S390X_INTER = {
    &FUNCS,
    &SC_NUMS,
    &REGS,
    0 /* no flags are defined for this architecture */,
    EM_S390,
    ELFDATA2MSB
};
