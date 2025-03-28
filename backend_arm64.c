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
static void store_to_byte(u8 src, u8 dst[4]) {
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
static void set_reg(u8 reg, i64 imm, sized_buf *dst_buf) {
    u16 default_val;
    mov_type lead_mt;

    if (imm < 0) {
        default_val = 0xffff;
        lead_mt = A64_MT_INVERT;
    } else {
        default_val = 0;
        lead_mt = A64_MT_ZERO;
    }

    /* split the immediate into 4 16-bit parts - high, medium-high, medium-low,
     * and low. */
    const struct {
        u16 imm16;
        shift_lvl shift;
    } parts[4] = {
        {imm, A64_SL_NO_SHIFT},
        {(imm >> 16), A64_SL_SHIFT16},
        {(imm >> 32), A64_SL_SHIFT32},
        {(imm >> 48), A64_SL_SHIFT48},
    };

    /* skip to the first part with non-default imm16 values. */
    bool started = false;
    for (ufast_8 i = 0; i < 4; i++) {
        if (parts[i].imm16 != default_val) {
            mov(started ? A64_MT_KEEP : lead_mt,
                parts[i].imm16,
                parts[i].shift,
                reg,
                sb_reserve(dst_buf, 4));
            started = true;
        }
    }
    if (!started) {
        /* all were the default value, so use this fallback instruction */
        /* (MOVZ|MOVN) x.reg, default_val */
        mov(lead_mt, default_val, A64_SL_NO_SHIFT, reg, sb_reserve(dst_buf, 4));
    }
}

/* MOV x.dst, x.src
 * technically an alias for ORR x.dst, XZR, x.src */
static void reg_copy(u8 dst, u8 src, sized_buf *dst_buf) {
    append_obj(dst_buf, (u8[]){0xe0 | dst, 0x03, src, 0xaa}, 4);
}

/* SVC 0 */
static void syscall(sized_buf *dst_buf) {
    append_obj(dst_buf, (u8[]){0x01, 0x00, 0x00, 0xd4}, 4);
}

#define NOP 0x1f, 0x20, 0x03, 0xd5

static void pad_loop_open(sized_buf *dst_buf) {
    /* BRK 1; NOP; NOP; */
    uchar instr_seq[3][4] = {{0x00}, {NOP}, {NOP}};
    serialize32le(0xd4200020, instr_seq[0]);
    append_obj(dst_buf, instr_seq, 12);
}

#undef NOP

/* LDRB w17, x.reg; TST w17, 0xff; B.cond offset */
static bool branch_cond(u8 reg, i64 offset, sized_buf *dst_buf, u8 cond) {
    if ((offset % 4) != 0) {
        internal_err(
            BF_ICE_INVALID_JUMP_ADDRESS,
            "offset is an invalid address offset (offset % 4 != 0)"
        );
    }
    /* use some bit shifts to check if the value is in range
     * (19 immediate bits are used, but as it must be a multiple of 4, it treats
     * them as though they're followed by an implicit 0b00, so it needs to fit
     * within the range of possible 21-bit 2's complement values. */
    if (!bit_fits(offset, 21)) {
        display_err((bf_comp_err){
            .id = BF_ERR_JUMP_TOO_LONG,
            .msg =
                "offset is outside the range of possible 21-bit signed values",
            .has_location = 0,
            .has_instr = 0,
        });
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
        internal_err(
            BF_ICE_IMMEDIATE_TOO_LARGE, "value is invalid for shift level."
        );
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

static void add_sub(u8 reg, arith_op op, u64 imm, sized_buf *dst_buf) {
    /* If the immediate fits within 12 bits, it's a far simpler process - simply
     * ADD or SUB the immediate. If it fits within 24 bits, use an ADD or SUB,
     * and shift the higher 12 of the 24 bits. If the 12 lower bits are
     * non-zero, then also ADD or SUB them afterwards.
     *
     * If the immediate does not fit within the lower 24 bits, then first set
     * x17 to the immediate, then ADD or SUB it. */
    if (imm < UINT64_C(0x1000)) {
        add_sub_imm(reg, imm, false, op, dst_buf);
    } else if (imm < UINT64_C(0x1000000)) {
        add_sub_imm(reg, imm & 0xfff000, true, op, dst_buf);
        if (imm & 0xfff) add_sub_imm(reg, imm & 0xfff, false, op, dst_buf);
    } else {
        /* different byte values are needed than normal here */
        u8 op_byte = (op == A64_OP_ADD) ? 0x8b : 0xcb;
        /* set register x17 to the target value */
        set_reg(TEMP_REG, (i64)imm, dst_buf);
        /* either ADD x.reg, x.reg, x17 or SUB x.reg, x.reg, x17 */
        serialize32le(
            (u32)op_byte << 24 | TEMP_REG << 16 | reg | reg << 5,
            sb_reserve(dst_buf, 4)
        );
    }
}

/* add_reg, sub_reg, inc_reg, and dec_reg are all simple wrappers around
 * add_sub. */
static void add_reg(u8 reg, u64 imm, sized_buf *dst_buf) {
    add_sub(reg, A64_OP_ADD, imm, dst_buf);
}

static void sub_reg(u8 reg, u64 imm, sized_buf *dst_buf) {
    add_sub(reg, A64_OP_SUB, imm, dst_buf);
}

static void inc_reg(u8 reg, sized_buf *dst_buf) {
    add_sub(reg, A64_OP_ADD, 1, dst_buf);
}

static void dec_reg(u8 reg, sized_buf *dst_buf) {
    add_sub(reg, A64_OP_SUB, 1, dst_buf);
}

/* to increment or decrement a byte, first load it into TEMP_REG,
 * then call an inner function (either inc_reg or dec_reg) on TEMP_REG,
 * and finally, store the least significant byte of TEMP_REG into the byte */
typedef void (*inc_dec_fn)(u8 reg, sized_buf *dst_buf);

static void inc_dec_byte(u8 reg, sized_buf *dst_buf, inc_dec_fn inner_fn) {
    load_from_byte(reg, sb_reserve(dst_buf, 4));
    inner_fn(TEMP_REG, dst_buf);
    store_to_byte(reg, sb_reserve(dst_buf, 4));
}

/* next, some thin wrappers around the inc_dec_byte function that can be used in
 * the arch_funcs struct */
static void inc_byte(u8 reg, sized_buf *dst_buf) {
    inc_dec_byte(reg, dst_buf, &inc_reg);
}

static void dec_byte(u8 reg, sized_buf *dst_buf) {
    inc_dec_byte(reg, dst_buf, &dec_reg);
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
static void zero_byte(u8 reg, sized_buf *dst_buf) {
    serialize32le(0x3800041f | reg << 5, sb_reserve(dst_buf, 4));
}

/* now, the last few thin wrapper functions */

static void add_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    add_sub_byte(reg, imm8, A64_OP_ADD, dst_buf);
}

static void sub_byte(u8 reg, u8 imm8, sized_buf *dst_buf) {
    add_sub_byte(reg, imm8, A64_OP_SUB, dst_buf);
}

const arch_inter ARM64_INTER = {
    .sc_read = 63,
    .sc_write = 64,
    .sc_exit = 93,
    .set_reg = set_reg,
    .reg_copy = reg_copy,
    .syscall = syscall,
    .pad_loop_open = pad_loop_open,
    .jump_zero = jump_zero,
    .jump_not_zero = jump_not_zero,
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
    .elf_arch = EM_AARCH64,
    .elf_data = ELFDATA2LSB,
    .reg_sc_num = 8 /* w8 */,
    .reg_arg1 = 0 /* x0 */,
    .reg_arg2 = 1 /* x1 */,
    .reg_arg3 = 2 /* x2 */,
    .reg_bf_ptr = 19 /* x19 */,
};

#ifdef BFC_TEST
/* internal */
#include "unit_test.h"

#define REF ARM64_DIS

static void test_set_reg_simple(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(24);

    struct {
        i64 imm;
        const char *disasm;
        u8 reg;
    } test_sets[4] = {
        {0, "mov x0, #0x0\n", 0},
        {-1, "mov x0, #-0x1\n", 0},
        {-0x100001, "mov x0, #-0x100001\n", 0},
        {0xbeef, "mov x1, #0xbeef\n", 1},
    };

    for (ufast_8 i = 0; i < 4; i++) {
        set_reg(test_sets[i].reg, test_sets[i].imm, &sb);
        DISASM_TEST(sb, dis, test_sets[i].disasm);
    }

    free(sb.buf);
    free(dis.buf);
}

static void test_reg_multiple(void) {
    sized_buf sb = newbuf(8);
    sized_buf dis = newbuf(32);

    set_reg(0, 0xdeadbeef, &sb);
    DISASM_TEST(
        sb,
        dis,
        "mov x0, #0xbeef\n"
        "movk x0, #0xdead, lsl #16\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_reg_split(void) {
    sized_buf sb = newbuf(8);
    sized_buf dis = newbuf(48);

    set_reg(19, 0xdead0000beef, &sb);
    DISASM_TEST(
        sb,
        dis,
        "mov x19, #0xbeef\n"
        "movk x19, #0xdead, lsl #32\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_reg_neg(void) {
    sized_buf sb = newbuf(8);
    sized_buf dis = newbuf(48);

    set_reg(19, INT64_C(-0xdeadbeef), &sb);
    DISASM_TEST(
        sb,
        dis,
        "mov x19, #-0xbeef\n"
        /* the bitwise negation of 0xdead is 0x2152 */
        "movk x19, #0x2152, lsl #16\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_reg_neg_split(void) {
    sized_buf sb = newbuf(12);
    sized_buf dis = newbuf(80);

    set_reg(19, INT64_C(-0xdead0000beef), &sb);
    DISASM_TEST(
        sb,
        dis,
        "mov x19, #-0xbeef\n"
        /* the bitwise negation of 0xdead is 0x2152 */
        "movk x19, #0x2152, lsl #32\n"
    );

    set_reg(8, INT64_C(-0xdeadbeef0000), &sb);
    DISASM_TEST(
        sb,
        dis,
        "mov x8, #-0x10000\n"
        /* the bitwise negation of 0xbeef is 0x4110
         * (Add 1 because that's how 2's complement works)*/
        "movk x8, #0x4111, lsl #16\n"
        /* the bitwise negation of 0xdead is 0x2152 */
        "movk x8, #0x2152, lsl #32\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_inc_dec_reg(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(24);

    inc_reg(0, &sb);
    DISASM_TEST(sb, dis, "add x0, x0, #0x1\n");

    inc_reg(19, &sb);
    DISASM_TEST(sb, dis, "add x19, x19, #0x1\n");

    dec_reg(1, &sb);
    DISASM_TEST(sb, dis, "sub x1, x1, #0x1\n");

    dec_reg(19, &sb);
    DISASM_TEST(sb, dis, "sub x19, x19, #0x1\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_load_store(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(24);

    load_from_byte(19, sb_reserve(&sb, 4));
    DISASM_TEST(sb, dis, "ldrb w17, [x19], #0x0\n");

    store_to_byte(19, sb_reserve(&sb, 4));
    DISASM_TEST(sb, dis, "strb w17, [x19], #0x0\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_reg(void) {
    sized_buf sb = newbuf(24);
    sized_buf dis = newbuf(64);
    add_sub(8, A64_OP_ADD, 0xabcdef, &sb);
    DISASM_TEST(sb, dis, "add x8, x8, #0xabc, lsl #12\nadd x8, x8, #0xdef\n");

    add_sub(8, A64_OP_SUB, 0xabc000, &sb);
    DISASM_TEST(sb, dis, "sub x8, x8, #0xabc, lsl #12\n");

    add_sub(8, A64_OP_ADD, 0xdeadbeef, &sb);
    add_sub(8, A64_OP_SUB, 0xdeadbeef, &sb);
    DISASM_TEST(
        sb,
        dis,
        "mov x17, #0xbeef\nmovk x17, #0xdead, lsl #16\nadd x8, x8, x17\n"
        "mov x17, #0xbeef\nmovk x17, #0xdead, lsl #16\nsub x8, x8, x17\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_byte(void) {
    sized_buf sb = newbuf(24);
    sized_buf dis = newbuf(144);

    add_byte(19, 0xa5, &sb);
    sub_byte(19, 0xa5, &sb);

    DISASM_TEST(
        sb,
        dis,
        "ldrb w17, [x19], #0x0\n"
        "add x17, x17, #0xa5\n"
        "strb w17, [x19], #0x0\n"
        "ldrb w17, [x19], #0x0\n"
        "sub x17, x17, #0xa5\n"
        "strb w17, [x19], #0x0\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_zero_byte(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(24);

    zero_byte(19, &sb);
    DISASM_TEST(sb, dis, "strb wzr, [x19], #0x0\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_inc_dec_wrapper(void) {
    sized_buf sb = newbuf(24);
    sized_buf dis = newbuf(136);
    inc_byte(1, &sb);
    dec_byte(8, &sb);
    DISASM_TEST(
        sb,
        dis,
        "ldrb w17, [x1], #0x0\n"
        "add x17, x17, #0x1\n"
        "strb w17, [x1], #0x0\n"
        "ldrb w17, [x8], #0x0\n"
        "sub x17, x17, #0x1\n"
        "strb w17, [x8], #0x0\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_reg_copy(void) {
    sized_buf sb = newbuf(12);
    sized_buf dis = newbuf(48);
    reg_copy(1, 19, &sb);
    reg_copy(2, 0, &sb);
    reg_copy(8, 8, &sb);
    DISASM_TEST(
        sb,
        dis,
        "mov x1, x19\n"
        "mov x2, x0\n"
        "mov x8, x8\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_syscall(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(16);
    syscall(&sb);
    DISASM_TEST(sb, dis, "svc #0\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_jmp_padding(void) {
    sized_buf sb = newbuf(12);
    sized_buf dis = newbuf(16);
    pad_loop_open(&sb);
    DISASM_TEST(sb, dis, "brk #0x1\nnop\nnop\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_successful_jumps(void) {
    sized_buf sb = newbuf(12);
    sized_buf dis = newbuf(104);
    jump_zero(0, 32, &sb);
    jump_not_zero(0, -32, &sb);
    DISASM_TEST(
        sb,
        dis,
        "ldrb w17, [x0], #0x0\n"
        "tst x17, #0xff\n"
        "b.eq #0x24\n"
        "ldrb w17, [x0], #0x0\n"
        "tst x17, #0xff\n"
        "b.ne #-0x1c\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_bad_jump_offset(void) {
    EXPECT_BF_ERR(BF_ICE_INVALID_JUMP_ADDRESS);
    jump_not_zero(0, 31, &(sized_buf){.buf = NULL, .sz = 0, .capacity = 0});
}

static void test_jump_too_long(void) {
    EXPECT_BF_ERR(BF_ERR_JUMP_TOO_LONG);
    jump_zero(0, 1 << 23, &(sized_buf){.buf = NULL, .sz = 0, .capacity = 0});
}

CU_pSuite register_arm64_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, test_set_reg_simple);
    ADD_TEST(suite, test_reg_multiple);
    ADD_TEST(suite, test_reg_split);
    ADD_TEST(suite, test_reg_neg);
    ADD_TEST(suite, test_reg_neg_split);
    ADD_TEST(suite, test_inc_dec_reg);
    ADD_TEST(suite, test_load_store);
    ADD_TEST(suite, test_add_sub_reg);
    ADD_TEST(suite, test_add_sub_byte);
    ADD_TEST(suite, test_zero_byte);
    ADD_TEST(suite, test_inc_dec_wrapper);
    ADD_TEST(suite, test_reg_copy);
    ADD_TEST(suite, test_syscall);
    ADD_TEST(suite, test_jmp_padding);
    ADD_TEST(suite, test_successful_jumps);
    ADD_TEST(suite, test_bad_jump_offset);
    ADD_TEST(suite, test_jump_too_long);
    return suite;
}

#endif /* BFC_TEST */
#endif /* BFC_TARGET_ARM64 */
