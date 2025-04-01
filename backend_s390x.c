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
 * * RI-c (2 halfwords, 12-bit opcode, [register, relative halfword immediate])
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
 * * RIL-c (3 halfwords, 12-bit opcode, [mask, relative word immediate])
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

static void store_to_byte(u8 reg, u8 aux, sized_buf *dst_buf) {
    /* STC aux, 0(reg) {RX-a} */
    u8 i_bytes[4] = {0x42, (aux << 4) | reg, 0x00, 0x00};
    append_obj(dst_buf, &i_bytes, 4);
}

static void load_from_byte(u8 reg, char dst[6]) {
    /* LLGC TMP_REG, 0(reg) {RXY-a} */
    memcpy(dst, (u8[6]){0xe3, (TMP_REG << 4) | reg, 0x00, 0x00, 0x00, 0x90}, 6);
}

/* declared before set_reg as it's used in set_reg, even though it's not first
 * in the struct. */
static void reg_copy(u8 dst, u8 src, sized_buf *dst_buf) {
    /* LGR dst, src {RRE} */
    append_obj(dst_buf, (u8[]){0xb9, 0x04, 0x00, (dst << 4) | src}, 4);
}

static void set_reg(u8 reg, i64 imm, sized_buf *dst_buf) {
    /* There are numerous ways to store immediates in registers for this
     * architecture. This function tries to find a way to load a given immediate
     * in as few machine instructions as possible, using shorter instructions
     * when available. No promise it actually is particularly efficient. */
    if (imm == 0) {
        /* copy from the zero register to reg */
        reg_copy(reg, 0, dst_buf);
    } else if (imm <= INT16_MAX && imm >= INT16_MIN) {
        /* if it fits in a halfword, use Load Halfword Immediate (64 <- 16) */
        /* LGHI r.reg, imm {RI-a} */
        u8 i_bytes[4] = ENCODE_RI_OP(0xa79, reg);
        serialize16be(imm, &i_bytes[2]);
        append_obj(dst_buf, &i_bytes, 4);
    } else if (imm <= INT32_MAX && imm >= INT32_MIN) {
        /* if it fits within a word, use Load Immediate (64 <- 32). */
        /* LGFI r.reg, imm {RIL-a} */
        u8 i_bytes[6] = ENCODE_RI_OP(0xc01, reg);
        serialize32be(imm, &i_bytes[2]);
        append_obj(dst_buf, &i_bytes, 6);
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
        set_reg(reg, (i32)imm, dst_buf);
        /* check if only one of the two higher quarters need to be explicitly
         * set, as that enables using shorter instructions. In the terminology
         * of the architecture, they are the high high and high low quarters of
         * the register's value. */
        if ((upper_imm & ~INT32_C(0xffff)) >> 16 == default_val) {
            /* sets bits 16-31 of the register to the immediate */
            /* IIHL reg, upper_imm {RI-a} */
            u8 i_bytes[4] = ENCODE_RI_OP(0xa51, reg);
            serialize16be(upper_imm, &i_bytes[2]);
            append_obj(dst_buf, &i_bytes, 4);
        } else if ((i16)upper_imm == default_val) {
            /* sets bits 0-15 of the register to the immediate. */
            /* IIHH reg, upper_imm {RI-a} */
            u8 i_bytes[4] = ENCODE_RI_OP(0xa50, reg);
            serialize16be(((u32)upper_imm) >> 16, &i_bytes[2]);
            append_obj(dst_buf, &i_bytes, 4);
        } else {
            /* need to set the full upper word, with Insert Immediate (high) */
            /* IIHF reg, imm {RIL-a} */
            u8 i_bytes[6] = ENCODE_RI_OP(0xc08, reg);
            serialize32be(upper_imm, &i_bytes[2]);
            append_obj(dst_buf, &i_bytes, 6);
        }
    }
}

static void syscall(sized_buf *dst_buf) {
    /* SVC 0 {I} */
    /* NOTE: on Linux s390x, if the SC number is less than 256, it it can be
     * passed as the second byte of the instruction, but taking advantage of
     * that would require refactoring - perhaps an extra parameter in the
     * arch_inter.syscall prototype, which would only be useful on this specific
     * architecture. The initial implementation of s390x must be complete and
     * working without any change outside of the designated insertion points. */
    append_obj(dst_buf, (u8[]){0x0a, 0x00}, 2);
}

typedef enum {
    MASK_EQ = 8,
    MASK_LT = 4,
    MASK_GT = 2,
    MASK_NE = MASK_LT | MASK_GT,
    MASK_NOP = 0
} comp_mask;

#define JUMP_SIZE 18

static bool branch_cond(
    u8 reg, i64 offset, comp_mask mask, char dst[JUMP_SIZE]
) {
    /* jumps are done by Halfwords, not bytes, so must ensure it's valid. */
    if ((offset % 2) != 0) {
        internal_err(
            BF_ICE_INVALID_JUMP_ADDRESS, "offset is not on a halfword boundary"
        );
        return false;
    }
    /* make sure offset is in range - the branch instructions take a 16-bit
     * offset of halfwords, so offset must be even and fit within a 17-bit
     * signed (2's complement) integer */
    if (offset < -0x10000 || offset > 0xffff) {
        display_err(basic_err(
            BF_ERR_JUMP_TOO_LONG, "offset is out-of-range for this architecture"
        ));
        return false;
    }
    /* addressing halfwords is possible in compare instructions, but not
     * addressing individual bytes, so instead load the byte of interest into
     * an auxiliary register and compare with that, much like the ARM
     * implementation. */
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
    /* load the value to compare with into the auxiliary register */
    load_from_byte(reg, dst);
    /* no need to serialize the immediate in the first instruction, as it's
     * already initialized to zero. The offset, on the other hand, still needs
     * to be set. Cast offset to u64 to avoid portability issues with signed
     * bit shifts. */
    serialize32be(((u64)offset >> 1), &i_bytes[1][2]);
    memcpy(&dst[6], i_bytes, 12);
    return true;
}

/* BRANCH ON CONDITION with all operands set to zero is used as a NO-OP.
 * Two different lengths are used. */
/* NOP is an extended mnemonic for BC 0, 0 {RX-b} */
#define NOP 0x47, 0x00, 0x00, 0x00
/* NOPR is an extended mnemonic for BCR 0, 0 {RR} */
#define NOPR 0x07, 0x00

static void pad_loop_open(sized_buf *dst_buf) {
    /* start with a jump into its own second haflword - both gcc and clang
     * generate that for `__builtin_trap()`.
     *
     * Follow up with NOP and NOPR instructions to pad to the needed size  */
    /* BRC 15, 0x2 {RI-c}; NOP; NOP; NOP; NOP; NOPR; */
    u8 i_bytes[18] = {0xa7, 0xf4, 0x00, 0x01, NOP, NOP, NOP, NOPR};
    append_obj(dst_buf, &i_bytes, 18);
}

static void add_reg_signed(u8 reg, i64 imm, sized_buf *dst_buf) {
    if (imm >= INT16_MIN && imm <= INT16_MAX) {
        /* if imm fits within a halfword, a shorter instruction can be used. */
        /* AGHI reg, imm {RI-a} */
        u8 i_bytes[4] = ENCODE_RI_OP(0xa7b, reg);
        serialize16be(imm, &i_bytes[2]);
        append_obj(dst_buf, &i_bytes, 4);
    } else if (imm >= INT32_MIN && imm <= INT32_MAX) {
        /* If imm fits within a word, then use a normal add immediate */
        /* AFGI reg, imm {RIL-a} */
        u8 i_bytes[6] = ENCODE_RI_OP(0xc28, reg);
        serialize32be(imm, &i_bytes[2]);
        append_obj(dst_buf, &i_bytes, 6);
    } else {
        /* if the lower 32 bits are non-zero, call this function recursively
         * to add to them */
        if ((i32)imm) add_reg_signed(reg, (i32)imm, dst_buf);

        /* add the higher 32 bits */
        /* AIH reg, imm {RIL-a} */
        u8 i_bytes[6] = ENCODE_RI_OP(0xcc8, reg);
        /* cast to u64 to avoid portability issues */
        serialize32be(((u64)imm >> 32), &i_bytes[2]);
        append_obj(dst_buf, &i_bytes, 6);
    }
}

static bool jump_open(u8 reg, i64 offset, sized_buf *dst_buf, size_t index) {
    return branch_cond(reg, offset, MASK_EQ, &dst_buf->buf[index]);
}

static bool jump_close(u8 reg, i64 offset, sized_buf *dst_buf) {
    return branch_cond(reg, offset, MASK_NE, sb_reserve(dst_buf, JUMP_SIZE));
}

static void add_reg(u8 reg, u64 imm, sized_buf *dst_buf) {
    add_reg_signed(reg, imm, dst_buf);
}

static void sub_reg(u8 reg, u64 imm, sized_buf *dst_buf) {
    /* there are not equivalent sub instructions to any of the add instructions
     * used, so take advantage of the fact that adding and subtracting INT64_MIN
     * have the same effect except for the possible effect on overflow flags
     * which eambfc never checks. */
    i64 imm_s = imm;
    if (imm_s != INT64_MIN) { imm_s = -imm_s; }
    add_reg_signed(reg, imm_s, dst_buf);
}

static void inc_reg(u8 reg, sized_buf *dst_buf) {
    add_reg_signed(reg, 1, dst_buf);
}

static void dec_reg(u8 reg, sized_buf *dst_buf) {
    add_reg_signed(reg, -1, dst_buf);
}

static void add_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    load_from_byte(reg, sb_reserve(dst_buf, 6));
    add_reg_signed(TMP_REG, imm8, dst_buf);
    store_to_byte(reg, TMP_REG, dst_buf);
}

static void sub_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    load_from_byte(reg, sb_reserve(dst_buf, 6));
    add_reg_signed(TMP_REG, -imm8, dst_buf);
    store_to_byte(reg, TMP_REG, dst_buf);
}

static void inc_byte(u8 reg, sized_buf *dst_buf) {
    add_byte(reg, 1, dst_buf);
}

static void dec_byte(u8 reg, sized_buf *dst_buf) {
    sub_byte(reg, 1, dst_buf);
}

static void zero_byte(u8 reg, sized_buf *dst_buf) {
    /* STC 0, 0(reg) {RX-a} */
    store_to_byte(reg, 0, dst_buf);
}

const arch_inter S390X_INTER = {
    .sc_read = 3,
    .sc_write = 4,
    .sc_exit = 1,
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
    .flags = 0 /* no flags are defined for this architecture */,
    .elf_arch = EM_S390,
    .elf_data = ELFDATA2MSB,
    .reg_sc_num = 1,
    .reg_arg1 = 2,
    .reg_arg2 = 3,
    .reg_arg3 = 4,
    /* NOTE: the s390x-abi specifies that registers r6 through r13, as well as
     * r15, are not clobbered by function calls. The linux kernel uses r6 and r7
     * for syscall args, not r8, so it should be fine to use.
     * See https://www.kernel.org/doc/html/v5.3/s390/debugging390.html */
    .reg_bf_ptr = 8,
};

#ifdef BFC_TEST
/* internal */
#include "unit_test.h"

/* Given that even though it is set to use hex immediates, the LLVM disassembler
 * for this architecture often uses decimal immediates, it's sometimes necessary
 * to explain why a given immediate is expected in the disassembly, so this
 * macro can be used as an enforced way to explain the reasoning */
#define GIVEN_THAT CU_ASSERT_FATAL
#define REF S390X_DIS

static void test_load_store(void) {
    sized_buf sb = newbuf(6);
    sized_buf dis = newbuf(24);

    load_from_byte(8, sb_reserve(&sb, 6));
    DISASM_TEST(sb, dis, "llgc %r5, 0(%r8,0)\n");
    memset(dis.buf, 0, dis.capacity);

    store_to_byte(8, 5, &sb);
    DISASM_TEST(sb, dis, "stc %r5, 0(%r8,0)\n");
    memset(dis.buf, 0, dis.capacity);

    store_to_byte(5, 8, &sb);
    DISASM_TEST(sb, dis, "stc %r8, 0(%r5,0)\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_reg_copy(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(16);

    reg_copy(2, 1, &sb);
    DISASM_TEST(sb, dis, "lgr %r2, %r1\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_set_reg_zero(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(16);
    sized_buf alt = newbuf(4);

    reg_copy(2, 0, &sb);
    set_reg(2, 0, &alt);
    CU_ASSERT_EQUAL(sb.sz, alt.sz);
    CU_ASSERT(memcmp(sb.buf, alt.buf, sb.sz) == 0);
    free(alt.buf);

    DISASM_TEST(sb, dis, "lgr %r2, %r0\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_set_reg_small_imm(void) {
    sized_buf sb = newbuf(8);
    sized_buf dis = newbuf(40);

    set_reg(5, 12345, &sb);
    set_reg(8, -12345, &sb);
    DISASM_TEST(sb, dis, "lghi %r5, 12345\nlghi %r8, -12345\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_set_reg_medium_imm(void) {
    sized_buf sb = newbuf(12);
    sized_buf dis = newbuf(48);

    set_reg(4, INT64_C(0x1234abcd), &sb);
    set_reg(4, INT64_C(-0x1234abcd), &sb);
    GIVEN_THAT(INT64_C(0x1234abcd) == INT64_C(305441741));
    DISASM_TEST(sb, dis, "lgfi %r4, 305441741\nlgfi %r4, -305441741\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_set_reg_large_imm(void) {
    sized_buf sb = newbuf(12);
    sized_buf dis = newbuf(48);

    set_reg(1, INT64_C(0xdead0000beef), &sb);
    GIVEN_THAT(0xdeadU == 57005U && 0xbeefU == 48879U);
    DISASM_TEST(sb, dis, "lgfi %r1, 48879\niihl %r1, 57005\n");
    memset(dis.buf, 0, dis.sz);

    set_reg(2, INT64_C(-0xdead0000beef), &sb);
    GIVEN_THAT(-0xbeefL == -48879L && ~(i16)0xdead == 8530);
    DISASM_TEST(sb, dis, "lgfi %r2, -48879\niihl %r2, 8530\n");
    memset(dis.buf, 0, dis.sz);

    set_reg(3, INT64_C(0xdead00000000), &sb);
    DISASM_TEST(sb, dis, "lgr %r3, %r0\niihl %r3, 57005\n");
    memset(dis.buf, 0, dis.sz);

    set_reg(4, INT64_MAX ^ (INT64_C(0xffff) << 32), &sb);
    DISASM_TEST(sb, dis, "lghi %r4, -1\niihh %r4, 32767\n");
    memset(dis.buf, 0, dis.sz);

    set_reg(5, INT64_MIN ^ (INT64_C(0xffff) << 32), &sb);
    DISASM_TEST(sb, dis, "lgr %r5, %r0\niihh %r5, 32768\n");
    memset(dis.buf, 0, dis.sz);

    set_reg(8, INT64_C(0x123456789abcdef0), &sb);
    GIVEN_THAT(0x12345678L == 305419896L);
    GIVEN_THAT((i32)0x9abcdef0UL == -1698898192L);
    DISASM_TEST(sb, dis, "lgfi %r8, -1698898192\niihf %r8, 305419896\n");
    memset(dis.buf, 0, dis.sz);

    set_reg(8, INT64_C(-0x123456789abcdef0), &sb);
    DISASM_TEST(sb, dis, "lgfi %r8, 1698898192\niihf %r8, 3989547399\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_successful_jumps(void) {
    sized_buf sb = newbuf(12);
    sized_buf dis = newbuf(128);
    sb_reserve(&sb, JUMP_SIZE);
    jump_open(3, 18, &sb, 0);
    jump_close(3, -36, &sb);
    pad_loop_open(&sb);
    /* For some reason, LLVM treats jump offset operand as an unsigned immediate
     * after sign extending it to the full 64 bits, so -36 becomes
     * 0xffffffffffffffdc */
    GIVEN_THAT((u64)INT64_C(-36) == UINT64_C(0xffffffffffffffdc));
    DISASM_TEST(

        sb,
        dis,
        "llgc %r5, 0(%r3,0)\n"
        "cfi %r5, 0\n"
        /* LLVM uses "jge" instead of the IBM-documented "jle" extended mnemonic
         * for `brcl 8,addr` because "jl" is also "jump if less". */
        "jge 0x12\n"
        "llgc %r5, 0(%r3,0)\n"
        "cfi %r5, 0\n"
        /* lh for low | high (i.e. not equal). */
        "jglh 0xffffffffffffffdc\n"
        /* instruction used for __builtin_trap() */
        "j 0x2\n"
        "nop 0\n"
        "nop 0\n"
        "nop 0\n"
        "nopr %r0\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_syscall(void) {
    sized_buf sb = newbuf(2);
    sized_buf dis = newbuf(8);

    syscall(&sb);
    CU_ASSERT_EQUAL(sb.sz, 2);
    DISASM_TEST(sb, dis, "svc 0\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_zero_byte(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(24);

    zero_byte(S390X_INTER.reg_bf_ptr, &sb);
    DISASM_TEST(sb, dis, "stc %r0, 0(%r8,0)\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_reg_arith_small_imm(void) {
    sized_buf a = newbuf(8);
    sized_buf b = newbuf(8);
    sized_buf dis = newbuf(40);

    add_reg(8, 1, &a);
    inc_reg(8, &b);
    /* check that inc_reg(r, sb) is the same as add_reg(r, 1, sb) */
    CU_ASSERT_EQUAL_FATAL(a.sz, b.sz);
    CU_ASSERT(memcmp(a.buf, b.buf, a.sz) == 0);
    DISASM_TEST(a, dis, "aghi %r8, 1\n");
    memset(dis.buf, 0, dis.sz);
    b.sz = 0;

    sub_reg(8, 1, &a);
    dec_reg(8, &b);
    /* check that dec_reg(r, sb) is the same as sub_reg(r, 1, sb) */
    CU_ASSERT_EQUAL_FATAL(a.sz, b.sz);
    CU_ASSERT(memcmp(a.buf, b.buf, a.sz) == 0);
    DISASM_TEST(a, dis, "aghi %r8, -1\n");
    memset(dis.buf, 0, dis.sz);
    b.sz = 0;

    /* check that sub_reg properly translates to add_reg */
    add_reg(8, 12345, &a);
    sub_reg(8, 12345, &a);
    sub_reg(8, INT64_C(-12345), &b);
    add_reg(8, INT64_C(-12345), &b);
    CU_ASSERT_EQUAL_FATAL(a.sz, b.sz);
    CU_ASSERT(memcmp(a.buf, b.buf, a.sz) == 0);
    DISASM_TEST(a, dis, "aghi %r8, 12345\naghi %r8, -12345\n");

    free(a.buf);
    free(b.buf);
    free(dis.buf);
}

static void test_reg_arith_medium_imm(void) {
    sized_buf sb = newbuf(6);
    sized_buf dis = newbuf(24);

    add_reg(8, 0x123456, &sb);
    GIVEN_THAT(0x123456 == 1193046);
    DISASM_TEST(sb, dis, "agfi %r8, 1193046\n");
    memset(dis.buf, 0, dis.sz);

    sub_reg(8, 0x123456, &sb);
    DISASM_TEST(sb, dis, "agfi %r8, -1193046\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_reg_arith_large_imm(void) {
    sized_buf sb = newbuf(12);
    sized_buf dis = newbuf(40);

    add_reg(8, INT64_C(9876543210), &sb);
    GIVEN_THAT((i32)INT64_C(9876543210) == 1286608618);
    GIVEN_THAT(INT64_C(9876543210) >> 32 == 2);
    DISASM_TEST(sb, dis, "agfi %r8, 1286608618\naih %r8, 2\n");
    memset(dis.buf, 0, dis.sz);

    sub_reg(8, INT64_C(9876543210), &sb);
    GIVEN_THAT((i32)INT64_C(-9876543210) == -1286608618);
    GIVEN_THAT(sign_extend((u64)INT64_C(-9876543210) >> 32, 32) == INT64_C(-3));
    DISASM_TEST(sb, dis, "agfi %r8, -1286608618\naih %r8, -3\n");
    memset(dis.buf, 0, dis.sz);

    /* make sure that if the lower bits are zero, the agfi instruction is
     * skipped */
    add_reg(8, 0x1234abcd00000000, &sb);
    GIVEN_THAT(0x1234abcd00000000 >> 32 == 305441741L);
    DISASM_TEST(sb, dis, "aih %r8, 305441741\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_i64_min(void) {
    sized_buf a = newbuf(6);
    sized_buf b = newbuf(6);

    add_reg(4, INT64_MIN, &a);
    sub_reg(4, INT64_MIN, &b);
    CU_ASSERT_EQUAL_FATAL(a.sz, b.sz);
    CU_ASSERT(memcmp(a.buf, b.buf, a.sz) == 0);

    free(a.buf);
    free(b.buf);
}

static void test_byte_arith(void) {
    sized_buf a = newbuf(14);
    sized_buf b = newbuf(14);
    sized_buf dis = newbuf(120);
    sized_buf expected = newbuf(14);

    load_from_byte(8, sb_reserve(&expected, 6));
    inc_reg(TMP_REG, &expected);
    store_to_byte(8, TMP_REG, &expected);
    inc_byte(8, &a);
    add_byte(8, 1, &b);
    CU_ASSERT_EQUAL_FATAL(a.sz, b.sz);
    CU_ASSERT_EQUAL_FATAL(a.sz, expected.sz);
    CU_ASSERT(memcmp(a.buf, b.buf, a.sz) == 0);
    CU_ASSERT(memcmp(a.buf, expected.buf, a.sz) == 0);
    DISASM_TEST(

        a,
        dis,
        "llgc %r5, 0(%r8,0)\n"
        "aghi %r5, 1\n"
        "stc %r5, 0(%r8,0)\n"
    );
    memset(dis.buf, 0, dis.sz);
    expected.sz = b.sz = 0;

    load_from_byte(8, sb_reserve(&expected, 6));
    dec_reg(TMP_REG, &expected);
    store_to_byte(8, TMP_REG, &expected);
    dec_byte(8, &a);
    sub_byte(8, 1, &b);
    CU_ASSERT_EQUAL_FATAL(a.sz, b.sz);
    CU_ASSERT_EQUAL_FATAL(a.sz, expected.sz);
    CU_ASSERT(memcmp(a.buf, b.buf, a.sz) == 0);
    CU_ASSERT(memcmp(a.buf, expected.buf, a.sz) == 0);
    DISASM_TEST(

        a,
        dis,
        "llgc %r5, 0(%r8,0)\n"
        "aghi %r5, -1\n"
        "stc %r5, 0(%r8,0)\n"
    );
    memset(dis.buf, 0, dis.sz);

    add_byte(8, 32, &a);
    sub_byte(8, 32, &a);
    DISASM_TEST(

        a,
        dis,
        "llgc %r5, 0(%r8,0)\n"
        "aghi %r5, 32\n"
        "stc %r5, 0(%r8,0)\n"
        "llgc %r5, 0(%r8,0)\n"
        "aghi %r5, -32\n"
        "stc %r5, 0(%r8,0)\n"
    );

    free(a.buf);
    free(b.buf);
    free(expected.buf);
    free(dis.buf);
}

static void test_bad_jump_offset(void) {
    EXPECT_BF_ERR(BF_ICE_INVALID_JUMP_ADDRESS);
    jump_close(0, 31, &(sized_buf){.buf = NULL, .sz = 0, .capacity = 0});
}

static void test_jump_too_long(void) {
    EXPECT_BF_ERR(BF_ERR_JUMP_TOO_LONG);
    jump_close(0, 1 << 23, &(sized_buf){.buf = NULL, .sz = 0, .capacity = 0});
}

CU_pSuite register_s390x_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, test_load_store);
    ADD_TEST(suite, test_reg_copy);
    ADD_TEST(suite, test_set_reg_zero);
    ADD_TEST(suite, test_set_reg_small_imm);
    ADD_TEST(suite, test_set_reg_medium_imm);
    ADD_TEST(suite, test_set_reg_large_imm);
    ADD_TEST(suite, test_successful_jumps);
    ADD_TEST(suite, test_syscall);
    ADD_TEST(suite, test_zero_byte);
    ADD_TEST(suite, test_reg_arith_small_imm);
    ADD_TEST(suite, test_reg_arith_medium_imm);
    ADD_TEST(suite, test_reg_arith_large_imm);
    ADD_TEST(suite, test_add_sub_i64_min);
    ADD_TEST(suite, test_byte_arith);
    ADD_TEST(suite, test_bad_jump_offset);
    ADD_TEST(suite, test_jump_too_long);
    return suite;
}

#endif /* BFC_TEST */

#endif /* BFC_TARGET_S390X */
