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
#include "serialize.h" /* serialize* */
#include "types.h" /* uint*_t, int*_t, bool, off_t, size_t, INT*_{MAX, MIN} */
#include "util.h" /* append_obj */

/* Information about this ISA is primarily from IBM's z/Architecture Reference
 * Summary, 11th Edition, available at the following URL as of 2024-10-29:
 * https://ibm.com/support/pages/sites/default/files/2021-05/SA22-7871-10.pdf
 *
 * The more comprehensive z/Architecture Principles of Operation was used when
 * more detail was needed. As of 2024-10-29, the 14th edition is available at
 * the following URL:
 * https://www.ibm.com/docs/en/module_1678991624569/pdf/SA22-7832-13.pdf
 *
 * Additional information comes from the ELF Application Binary Interface s390x
 * Supplement, Version 1.6.1, which is available from IBM's GitHub:
 * https://github.com/IBM/s390x-abi/releases/download/v1.6.1/lzsabi_s390x.pdf
 *
 * Finally, some information is from examining existing s390x binaries with the
 * rasm2 dis/assembler from the Radare2 project - mainly my own implementation
 * of a minimal 'clear' command, made in a hex editor.
 * https://rada.re/n/radare2.html
 * https://github.com/eliminmax/tiny-clear-elf/tree/main/s390x/ */

/* 2 common formats (RI-a and RIL-a) for instructions that use immediates uses
 * 16 bits for the opcode and register, followed by the actual immediate value.
 * The only difference between them is the number of immediate bytes included -
 * RI-a uses a Halfword Immediate, which is 16 bits, while RIL-a uses a 32-bit
 * immediate. The following can be used to initialize an array with the first 16
 * bits set, only needing to serialize the remaining bits as needed. */
#define ENCODE_OP_REG(op, reg) { (op) >> 4, ((reg) << 4) | ((op) & 0xf) }

static bool set_reg(uint8_t reg, int64_t imm, sized_buf *dst_buf){
    /* There are numerous ways to store immediates in registers for this
     * architecture. This function tries to find a way to load a given immediate
     * in as few machine instructions as possible, using shorter instructions
     * when available. No promise it actualy is particularly efficient. */
    if (imm <= INT16_MAX && imm >= INT16_MIN) {
        /* if it fits within a halfword (i.e. 16 bits) use the Load Halfword
         * Immediate instruction, which is in the RI-a format.
        /* LGHI r.reg, imm */
        uint8_t i_bytes[4] = ENCODE_OP_REG(0xa79, reg);
        return serialize16be(imm, &i_bytes[2]) == 2 &&
                append_obj(dst_buf, &i_bytes, 4);
    } else if (imm <= INT32_MAX && imm >= INT32_MIN) {
        /* if it fits within a word (i.e. 32 bits) use the Load Immediate
         * instruction, which is in the RIL-a format, meaning that it is encoded
         * in the same way as the RI-a format, but with a 32-bit instead of a
         * 16-bit immediate. */
        /* LGFI r.reg, imm */
        uint8_t i_bytes[6] = ENCODE_OP_REG(0xc01, reg);
        return serialize32be(imm, &i_bytes[2]) == 4 &&
                append_obj(dst_buf, &i_bytes, 6);
    } else {
        /* if it does not fit within 32 bits, then the lower 32 bits need to be
         * set as normal, then the higher 32 bits need to be set. Cast imm to
         * a 32-bit value and call this function recursively to handle the lower
         * 32 bits, then use an "insert immediate" instruction to set the higher
         * bits. */
        int16_t default = (imm >= 0) ? 0 : -1;
        int32_t upper_imm = imm >> 32;
        /* try to set the upper bits no matter what, but if the lower bits
         * failed, still want to return false. */
        bool ret = set_reg(reg, (int32_t)imm, dst_buf);
        if (upper_imm <= INT16_MAX && upper_imm >= INT16_MIN) {
            /* Insert Immediate (high low) is in RI-a format, much like LGHI. */
            /* IIHL reg, upper_imm */
            uint8_t i_bytes[4] = ENCODE_OP_REG(0xa51, reg);
            ret &= (serialize16be(upper_imm, &i_bytes[2]) == 2) &&
                append_obj(dst_buf, &i_bytes, 4);
        } else if ((int16_t)upper_imm == default) {
            /* in this case, the lower half-word of the upper half don't need
             * to be explicitly set, so use the Insert Immediate (high high)
             * instruction, which is also in RI-a format. */
            /* IIHH reg, upper_imm */
            uint8_t i_bytes[4] = ENCODE_OP_REG(0xa50, reg);
            ret &= (serialize16be(upper_imm, &i_bytes[2]) == 2) &&
                append_obj(dst_buf, &i_bytes, 4);
        } else {
            /* no shortcuts, just set the upper 32 bits directly with
             * Insert Immediate (high) */
            /* IIHF reg, imm */
            uint8_t i_bytes[6] = ENCODE_OP_REG(0xc09, reg);
            ret &= (serialize32be(upper_imm, &i_bytes[2]) == 4) &&
                append_obj(dst_buf, &i_bytes, 6);
        }
        return ret;
    }
}

static bool reg_copy(uint8_t dst, uint8_t src, sized_buf *dst_buf){
    /* LGR dst, src */
    return append_obj(dst_buf, (uint8_t[]){ 0xb9, 0x04, 0x00, (dst<<4)|src}, 4);
}

static bool syscall(sized_buf *dst_buf){
    /* SVC 0 - second byte, if non-zero, is the system call number.
     * On s390x, if the SC number is less than 256, it it can be passed as the
     * second byte of the instruction, but taking advantage of that would
     * require refactoring - perhaps an extra parameter in the
     * arch_inter.syscall prototype, which would only be useful on this specific
     * architecture. The initial implementation of s390x must be complete and
     * working without any change outside of the designated insertion points. */
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
    8 /* bf_ptr */
    /* NOTE: the s390x-abi specifies that registers r6 through r13, as well as
     * r15, are not clobbered by function calls. The linux kernel uses r6 and r7
     * for syscall args, not r8, so it should be fine to use.
     * See https://www.kernel.org/doc/html/v5.3/s390/debugging390.html */
};


const arch_inter S390X_INTER = {
    &FUNCS,
    &SC_NUMS,
    &REGS,
    0 /* no flags are defined for this architecture */,
    EM_S390,
    ELFDATA2MSB
};
