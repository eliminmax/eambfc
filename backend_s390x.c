/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * The implementation for IBM z/Architecture systems. Not actually expected to
 * run on any IBM mainframes, but I wanted to be sure that big-endian systems
 * are properly supported. */

/* internal */
#include "arch_inter.h"
#include "compat/elf.h"
#include "config.h"
#include "err.h"
#include "serialize.h"
#include "types.h"
#include "util.h"

#if BFC_TARGET_S390X

/* The z/Architecture Principles of Operation comprehensively documents the
 * z/Architecture ISA, and its 14th edition was the main source for information
 * about the architecture when writing this backend. As of 2024-10-29, IBM
 * provides a PDF of that edition at the following URL:
 * https://www.ibm.com/docs/en/module_1678991624569/pdf/SA22-7832-13.pdf
 *
 * The z/Architecture Reference Summary provides a selection of the information
 * from the Principles of Operation in a more concise form, and is a helpful
 * supplement to it. As of 2024-10-29, IBM provides the 11th edition at the
 * following URL:
 * https://ibm.com/support/pages/sites/default/files/2021-05/SA22-7871-10.pdf
 *
 * Additional information about the ISA is available in the ELF Application
 * Binary Interface s390x Supplement, Version 1.6.1. As f 2024-10-29, IBM
 * provides it at the following URL:
 * https://github.com/IBM/s390x-abi/releases/download/v1.6.1/lzsabi_s390x.pdf
 *
 * Information about the Linux Kernel's use of different registers was obtained
 * from the "Debugging on Linux for s/390 & z/Architecture" page in the docs for
 * Linux 5.3.0, available at the following URL:
 * https://www.kernel.org/doc/html/v5.3/s390/debugging390.html
 *
 * Finally, some information was gleaned from examining existing s390x binaries
 * with the rasm2 assembler/disassembler from the Radare2 project - mainly an
 * implementation of a minimal 'clear' command, made in a hex editor.
 * https://rada.re/n/radare2.html
 * https://github.com/eliminmax/tiny-clear-elf/tree/main/s390x/ */

/* ISA Information
 *
 * This is explained here, rather than being repeated throughout the comments
 * throughout this file.
 *
 * the z/Architecture ISA has 16 general-purpose registers, r0 to r15. If any
 * value other than zero is stored in r0, an exception occurs, so it can always
 * be assumed to contain zero.
 *
 * Some instructions take a memory operand, which consists of a 12-bit
 * displacement 'd', an optional index register 'x', and an optional base
 * register 'b'. Others take a 20-bit displacement, split into the 12 lower bits
 * 'dl', and the 8 higher bits 'dh'. In both cases, the values in the index and
 * base registers are added to the displacement to get a memory address.
 *
 * Bits are grouped into 8-bit "bytes", as is almost universal.
 *
 * Bytes can be grouped into larger structures, most commonly 2-byte
 * "halfwords", though 4-byte "words", 8-byte "doublewords", and more exist.
 * The full list is on page "3-4" of the Principles of Operation book, but what
 * must be understood is that they must be aligned properly (e.g. half-words
 * must start on even-numbered bytes).
 *
 * Instructions are 1, 2, or 3 halfwords long.
 *
 * There are numerous formats for instructions, which are given letter codes,
 * such as E, I, IE, MII, RI-a, and so on. They are listed in the Reference
 * Summary, starting on page #1, which is actually the 13th page of the PDF.
 *
 * The instruction formats used in eambfc are listed below:
 *
 * * I (1 halfword, 8-bit opcode, [byte immediate])
 *  - bits 0-8: opcode
 *  - bits 8-16: immediate
 *
 * * RI-a (2 halfwords, 12-bit opcode, [register, halfword immediate])
 *  - bits 0-7: higher 8 bits of opcode
 *  - bits 8-11: register
 *  - bits 12-15: lower 4 bits of opcode
 *  - bits 16-31: immediate
 *
 * * RIL-a (3 halfwords, 12-bit opcode, [register, word immediate])
 *  - bits 0-7: higher 8 bits of opcode
 *  - bits 8-11: register
 *  - bits 12-15: lower 4 bits of opcode
 *  - bits 16-47: immediate
 *
 * * RIL-c (3 halfwords, 12-bit opcode, [mask, 32-bit relative immediate])
 *  - bits 0-7: higher 8 bits of opcode
 *  - bits 8-11: mask
 *  - bits 12-15: lower 4 bits of opcode
 *  - bits 16-47: relative immediate
 *
 * * RX-a (2 halfwords, 8-bit opcode, [register, memory])
 *  - bits 0-7: opcode
 *  - bits 8-11: register
 *  - bits 12-15: memory index register
 *  - bits 16-19: memory base register
 *  - bits 20-31: memory displacement
 *
 * * RX-b (2 halfwords, 8-bit opcode, [mask, memory])
 *  - bits 0-7: opcode
 *  - bits 8-11: mask
 *  - bits 12-15: memory index register
 *  - bits 16-19: memory base register
 *  - bits 20-31: memory displacement
 *
 * * RXY-a (3 halfwords, 16-bit opcode, [register, extended memory])
 *  - bits 0-7: higher 8 bits of opcode
 *  - bits 8-11: register
 *  - bits 12-15: memory index register
 *  - bits 16-19: memory base register
 *  - bits 20-31: memory displacement (lower 12 bits)
 *  - bits 32-39: memory displacement (higher 8 bits)
 *  - bits 40-47: lower 8 bits of opcode
 *
 * * RR (1 halfword, 8-bit opcode, [register or mask, register])
 *  - bits 0-7: opcode
 *  - bits 8-11: first register or mask
 *  - bits 12-15: second register
 *
 * * RRE (2 halfwords, 16-bit opcode, [register, register])
 *  - bits 0-15: opcode
 *  - bits 16-23: unassigned (must be set to 0)
 *  - bits 24-27: first register
 *  - bits 28-31: second register
 *
 * As with other backends, when a machine instruction appears, it has the
 * corresponding assembly in a comment nearby. Unlike other backends, that
 * comment is followed by the instruction format in curly braces. */

/* 3 common formats within eambfc (RI-a, RIL-c, and RIL-a) have 12-bit opcodes,
 * with a 4-bit operand between the higher 8 and lower 4 bits of the opcodes
 * within the highest 16 bits, followed by an immediate, with the differences
 * being the operand types and immediate size.
 * The following macro can initialize an array for any of the three, and the
 * appropriately-sized serialize{16,32}be function can be used for the actual
 * immediate value after initializing the array. */
#define ENCODE_RI_OP(op, reg) {(op) >> 4, ((reg) << 4) | ((op) & 0xf)}

/* a call-clobbered register to use as a temporary scratch register */
static const u8 TMP_REG = UINT8_C(5);

static bool store_to_byte(u8 reg, u8 aux, sized_buf *dst_buf) {
    /* STC aux, 0(reg) {RX-a} */
    u8 i_bytes[4] = {0x42, (aux << 4) | reg, 0x00, 0x00};
    return append_obj(dst_buf, &i_bytes, 4);
}

static bool load_from_byte(u8 reg, sized_buf *dst_buf) {
    /* LLGC TMP_REG, 0(reg) {RXY-a} */
    u8 i_bytes[6] = {0xe3, (TMP_REG << 4) | reg, 0x00, 0x00, 0x00, 0x90};
    return append_obj(dst_buf, &i_bytes, 6);
}

/* declared before set_reg as it's used in set_reg, even though it's not first
 * in the struct. */
static bool reg_copy(u8 dst, u8 src, sized_buf *dst_buf) {
    /* LGR dst, src {RRE} */
    return append_obj(dst_buf, (u8[]){0xb9, 0x04, 0x00, (dst << 4) | src}, 4);
}

static bool set_reg(u8 reg, i64 imm, sized_buf *dst_buf) {
    /* There are numerous ways to store immediates in registers for this
     * architecture. This function tries to find a way to load a given immediate
     * in as few machine instructions as possible, using shorter instructions
     * when available. No promise it actually is particularly efficient. */
    if (imm == 0) {
        /* copy from the zero register to reg */
        return reg_copy(reg, 0, dst_buf);
    } else if (imm <= INT16_MAX && imm >= INT16_MIN) {
        /* if it fits in a halfword, use Load Halfword Immediate (64 <- 16) */
        /* LGHI r.reg, imm {RI-a} */
        u8 i_bytes[4] = ENCODE_RI_OP(0xa79, reg);
        serialize16be(imm, &i_bytes[2]);
        return append_obj(dst_buf, &i_bytes, 4);
    } else if (imm <= INT32_MAX && imm >= INT32_MIN) {
        /* if it fits within a word, use Load Immediate (64 <- 32). */
        /* LGFI r.reg, imm {RIL-a} */
        u8 i_bytes[6] = ENCODE_RI_OP(0xc01, reg);
        serialize32be(imm, &i_bytes[2]);
        return append_obj(dst_buf, &i_bytes, 6);
    } else {
        /* if it does not fit within 32 bits, then the lower 32 bits need to be
         * set as normal, then the higher 32 bits need to be set. Cast imm to
         * a 32-bit value and call this function recursively to handle the lower
         * 32 bits, then use an "insert immediate" instruction to set the higher
         * bits. */
        i16 default_val = (imm >= 0) ? INT16_C(0) : INT16_C(-1);

        /* casting to avoid portability issues */
        i32 upper_imm = (u64)imm >> 32;

        /* try to set the upper bits no matter what, but if the lower bits
         * failed, still want to return false. */
        bool ret = set_reg(reg, (i32)imm, dst_buf);
        /* check if only one of the two higher quarters need to be explicitly
         * set, as that enables using shorter instructions. In the terminology
         * of the architecture, they are the high high and high low quarters of
         * the register's value. */
        if (upper_imm <= INT16_MAX && upper_imm >= INT16_MIN) {
            /* sets bits 16-31 of the register to the immediate */
            /* IIHL reg, upper_imm {RI-a} */
            u8 i_bytes[4] = ENCODE_RI_OP(0xa51, reg);
            serialize16be(upper_imm, &i_bytes[2]);
            ret &= append_obj(dst_buf, &i_bytes, 4);
        } else if ((i16)upper_imm == default_val) {
            /* sets bits 0-15 of the register to the immediate. */
            /* IIHH reg, upper_imm {RI-a} */
            u8 i_bytes[4] = ENCODE_RI_OP(0xa50, reg);
            serialize16be(upper_imm, &i_bytes[2]);
            ret &= append_obj(dst_buf, &i_bytes, 4);
        } else {
            /* need to set the full upper word, with Insert Immediate (high) */
            /* IIHF reg, imm {RIL-a} */
            u8 i_bytes[6] = ENCODE_RI_OP(0xc08, reg);
            serialize32be(upper_imm, &i_bytes[2]);
            ret &= append_obj(dst_buf, &i_bytes, 6);
        }
        return ret;
    }
}

static bool syscall(sized_buf *dst_buf) {
    /* SVC 0 {I} */
    /* NOTE: on Linux s390x, if the SC number is less than 256, it it can be
     * passed as the second byte of the instruction, but taking advantage of
     * that would require refactoring - perhaps an extra parameter in the
     * arch_inter.syscall prototype, which would only be useful on this specific
     * architecture. The initial implementation of s390x must be complete and
     * working without any change outside of the designated insertion points. */
    return append_obj(dst_buf, (u8[]){0x0a, 0x00}, 2);
}

typedef enum {
    MASK_EQ = 8,
    MASK_LT = 4,
    MASK_GT = 2,
    MASK_NE = MASK_LT | MASK_GT,
    MASK_NOP = 0
} comp_mask;

static bool branch_cond(u8 reg, i64 offset, comp_mask mask, sized_buf *dst) {
    /* jumps are done by Halfwords, not bytes, so must ensure it's valid. */
    if ((offset % 2) != 0) {
        basic_err(
            "INVALID_JUMP_ADDRESS", "offset is not on a halfword boundary"
        );
        return false;
    }
    /* make sure offset is in range - the branch instructions take a 16-bit
     * offset of halfwords, so offset must be even and fit within a 17-bit
     * signed (2's complement) integer */
    if (offset < -0x10000 || offset > 0xffff) {
        basic_err(
            "JUMP_TOO_LONG", "offset is out-of-range for this architecture"
        );
        return false;
    }
    /* addressing halfwords is possible in compare instructions, but not
     * addressing individual bytes, so instead load the byte of interest into
     * an auxiliary register and compare with that, much like the ARM
     * implementation. */
    /* load the value to compare with into the auxiliary register */
    bool ret = load_from_byte(reg, dst);
    /* set condition code according to contents of the auxiliary register, then
     * conditionally branch if the condition code's corresponding mask bit is
     * set to one.
     *
     * More specifically, if the auxiliary register contains the value 0, then
     * the condition code will be 0b1000 If it's less than 0, then it will be
     * 0b0100, and if it's more than zero, it will be 0b0010.
     * That's according to page "C-2" of the Principles of Operation.
     *
     * in pseudocode:
     * | switch (TMP_REG) {
     * |   case 0: condition_code = 0b1000;
     * |   case i if i < 0: condition_code = 0b0100;
     * |   case i if i > 0: condition_code = 0b0010;
     * | }
     * |
     * | if (condition_code & mask) {
     * |    jump (offset);
     * | }
     *
     * */

    /* CFI TMP_REG, 0 {RIL-a}; BRCL mask, offset; */
    u8 i_bytes[2][6] = {
        ENCODE_RI_OP(0xc2d, TMP_REG),
        ENCODE_RI_OP(0xc04, mask),
    };
    /* no need to serialize the immediate in the first instruction, as it's
     * already initialized to zero. The offset, on the other hand, still needs
     * to be set. Cast offset to u64 to avoid portability issues with signed
     * bit shifts. */
    serialize32be(((u64)offset >> 1), &i_bytes[1][2]);
    ret &= append_obj(dst, &i_bytes, 12);

    return ret;
}

/* BRANCH ON CONDITION with all operands set to zero is used as a NO-OP.
 * Two different lengths are used. */
/* NOP is an extended mnemonic for BC 0, 0 {RX-b} */
#define NOP 0x47, 0x00, 0x00, 0x00
/* NOPR is an extended mnemonic for BCR 0, 0 {RR} */
#define NOPR 0x07, 0x00

static bool nop_loop_open(sized_buf *dst_buf) {
    u8 i_bytes[18] = {NOP, NOP, NOP, NOP, NOPR};
    return append_obj(dst_buf, &i_bytes, 18);
}

static bool add_reg_signed(u8 reg, i64 imm, sized_buf *dst_buf) {
    if (imm >= INT16_MIN && imm <= INT16_MAX) {
        /* if imm fits within a halfword, a shorter instruction can be used. */
        /* AGHI reg, imm {RI-a} */
        u8 i_bytes[4] = ENCODE_RI_OP(0xa7b, reg);
        serialize16be(imm, &i_bytes[2]);
        return append_obj(dst_buf, &i_bytes, 4);
    } else if (imm >= INT32_MIN && imm <= INT32_MAX) {
        /* If imm fits within a word, then use a normal add immediate */
        /* AFGI reg, imm {RIL-a} */
        u8 i_bytes[6] = ENCODE_RI_OP(0xc28, reg);
        serialize32be(imm, &i_bytes[2]);
        return append_obj(dst_buf, &i_bytes, 6);
    } else {
        /* if the lower 32 bits are non-zero, call this function recursively
         * to add to them */
        bool ret = ((i32)imm == 0) || add_reg_signed(reg, (i32)imm, dst_buf);

        /* add the higher 32 bits */
        /* AIH reg, imm {RIL-a} */
        u8 i_bytes[6] = ENCODE_RI_OP(0xcc8, reg);
        /* cast to u64 to avoid portability issues */
        serialize32be(((u64)imm >> 32), &i_bytes[2]);
        ret &= append_obj(dst_buf, &i_bytes, 6);
        return ret;
    }
}

static bool jump_zero(u8 reg, i64 offset, sized_buf *dst_buf) {
    return branch_cond(reg, offset, MASK_EQ, dst_buf);
}

static bool jump_not_zero(u8 reg, i64 offset, sized_buf *dst_buf) {
    return branch_cond(reg, offset, MASK_NE, dst_buf);
}

static bool add_reg(u8 reg, u64 imm, sized_buf *dst_buf) {
    return add_reg_signed(reg, imm, dst_buf);
}

static bool sub_reg(u8 reg, u64 imm, sized_buf *dst_buf) {
    /* there are not equivalent sub instructions to any of the add instructions
     * used, so take advantage of the fact that adding and subtracting INT64_MIN
     * have the same effect except for the possible effect on overflow flags
     * which eambfc never checks. */
    i64 imm_s = imm;
    if (imm_s != INT64_MIN) { imm_s = -imm_s; }
    return add_reg_signed(reg, imm_s, dst_buf);
}

static bool inc_reg(u8 reg, sized_buf *dst_buf) {
    return add_reg_signed(reg, 1, dst_buf);
}

static bool dec_reg(u8 reg, sized_buf *dst_buf) {
    return add_reg_signed(reg, -1, dst_buf);
}

static bool add_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    bool ret = load_from_byte(reg, dst_buf);
    ret &= add_reg_signed(TMP_REG, imm8, dst_buf);
    ret &= store_to_byte(reg, TMP_REG, dst_buf);
    return ret;
}

static bool sub_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    bool ret = load_from_byte(reg, dst_buf);
    ret &= add_reg_signed(TMP_REG, -imm8, dst_buf);
    ret &= store_to_byte(reg, TMP_REG, dst_buf);
    return ret;
}

static bool inc_byte(u8 reg, sized_buf *dst_buf) {
    return add_byte(reg, 1, dst_buf);
}

static bool dec_byte(u8 reg, sized_buf *dst_buf) {
    return sub_byte(reg, 1, dst_buf);
}

static bool zero_byte(u8 reg, sized_buf *dst_buf) {
    /* STC 0, 0(reg) {RX-a} */
    return store_to_byte(reg, 0, dst_buf);
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
    .read = 3,
    .write = 4,
    .exit = 1,
};

static const arch_registers REGS = {
    .sc_num = 1,
    .arg1 = 2,
    .arg2 = 3,
    .arg3 = 4,
    /* NOTE: the s390x-abi specifies that registers r6 through r13, as well as
     * r15, are not clobbered by function calls. The linux kernel uses r6 and r7
     * for syscall args, not r8, so it should be fine to use.
     * See https://www.kernel.org/doc/html/v5.3/s390/debugging390.html */
    .bf_ptr = 8,
};

const arch_inter S390X_INTER = {
    .FUNCS = &FUNCS,
    .SC_NUMS = &SC_NUMS,
    .REGS = &REGS,
    .FLAGS = 0 /* no flags are defined for this architecture */,
    .ELF_ARCH = EM_S390,
    .ELF_DATA = ELFDATA2MSB,
};
#endif /* BFC_TARGET_S390X */
