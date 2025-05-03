/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file provides the arch_inter for the x86_64 architecture. */
/* internal */
#include <config.h>
#include <types.h>

#include "arch_inter.h"
#include "err.h"
#include "serialize.h"
#include "util.h"
#include "x86_common.h"

#if BFC_TARGET_I386

const char *VAL_TRUNCATED_WARNING =
    "value truncated as it exceeds 32-bit register size";

static nonnull_arg(4) bool reg_arith(
    u8 reg,
    u64 imm,
    arith_op op,
    sized_buf *restrict dst_buf,
    bf_comp_err *restrict err
) {
    if (imm == 0) {
        return true;
    } else if (imm <= INT8_MAX) {
        /* ADD/SUB reg, byte imm */
        append_obj(dst_buf, (uchar[]){0x83, op + reg, imm}, 3);
    } else {
        /* ADD/SUB reg, imm */
        uchar i_bytes[6] = {0x81, op + reg};
        serialize32le(imm, &(i_bytes[2]));
        append_obj(dst_buf, &i_bytes, 6);
        if (imm > UINT32_MAX) {
            if (err) {
                *err = (bf_comp_err){
                    .msg.ref = VAL_TRUNCATED_WARNING,
                    .id = BF_ERR_CODE_TOO_LARGE,
                };
            }
            return false;
        }
    }
    return true;
}

/* now, the functions exposed through I386_INTER */
/* use the most efficient way to set a register to imm */
static nonnull_arg(3) bool set_reg(
    u8 reg, i64 imm, sized_buf *restrict dst_buf, bf_comp_err *restrict err
) {
    if (imm == 0) {
        /* XOR reg, reg */
        append_obj(
            dst_buf, (u8[]){INSTRUCTION(0x31, 0xc0 | (reg << 3) | reg)}, 2
        );
    } else if (imm >= INT32_MIN && imm <= INT32_MAX) {
        /* MOV reg, imm32 */
        u8 instr_bytes[5] = {INSTRUCTION(0xb8 | reg, IMM32_PADDING)};
        serialize32le(imm, &(instr_bytes[1]));
        append_obj(dst_buf, &instr_bytes, 5);
    } else {
        if (err) {
            *err = (bf_comp_err){
                .msg.ref = VAL_TRUNCATED_WARNING,
                .id = BF_ERR_CODE_TOO_LARGE,
            };
        }
        return false;
    }
    return true;
}

/* SET EAX, sc_num; INT 0x80 */
static nonnull_args void syscall(sized_buf *restrict dst_buf, u32 sc_num) {
    set_reg(X86_EAX, sc_num, dst_buf, NULL);
    append_obj(dst_buf, (u8[]){INSTRUCTION(0xcd, 0x80)}, 2);
}

/* INC reg */
static nonnull_args void inc_reg(u8 reg, sized_buf *restrict dst_buf) {
    append_obj(dst_buf, (uchar[]){0xff, 0xc0 | reg}, 2);
}

/* DEC reg */
static nonnull_args void dec_reg(u8 reg, sized_buf *restrict dst_buf) {
    append_obj(dst_buf, (uchar[]){0xff, 0xc8 | reg}, 2);
}

static nonnull_arg(3) bool add_reg(
    u8 reg, u64 imm, sized_buf *restrict dst_buf, bf_comp_err *restrict err
) {
    return reg_arith(reg, imm, X64_OP_ADD, dst_buf, err);
}

static nonnull_arg(3) bool sub_reg(
    u8 reg, u64 imm, sized_buf *restrict dst_buf, bf_comp_err *restrict err
) {
    return reg_arith(reg, imm, X64_OP_SUB, dst_buf, err);
}

const arch_inter I386_INTER = {
    .sc_read = 3,
    .sc_write = 4,
    .sc_exit = 1,
    .set_reg = set_reg,
    .reg_copy = x86_reg_copy,
    .syscall = syscall,
    .pad_loop_open = x86_pad_loop_open,
    .jump_open = x86_jump_open,
    .jump_close = x86_jump_close,
    .inc_reg = inc_reg,
    .dec_reg = dec_reg,
    .inc_byte = x86_inc_byte,
    .dec_byte = x86_dec_byte,
    .add_reg = add_reg,
    .sub_reg = sub_reg,
    .add_byte = x86_add_byte,
    .sub_byte = x86_sub_byte,
    .zero_byte = x86_zero_byte,
    .flags = 0 /* no flags are defined for this architecture */,
    .elf_arch = 3 /* EM_386 */,
    .elf_data = BYTEORDER_LSB,
    .addr_size = PTRSIZE_32,
    .reg_sc_num = X86_EAX,
    .reg_arg1 = X86_EBX,
    .reg_arg2 = X86_ECX,
    .reg_arg3 = X86_EDX,
    .reg_bf_ptr = X86_ESI,
};

#ifdef BFC_TEST

#include "unit_test.h"
#define REF I386_DIS

static void test_set_reg(void) {
    sized_buf sb = newbuf(10);
    sized_buf dis = newbuf(32);

    CU_ASSERT(set_reg(X86_EBX, 0, &sb, NULL));
    DISASM_TEST(sb, dis, "xor ebx, ebx\n");

    CU_ASSERT(set_reg(X86_EBX, 128, &sb, NULL));
    DISASM_TEST(sb, dis, "mov ebx, 0x80\n");

    CU_ASSERT(set_reg(X86_EBX, INT32_MAX - INT32_C(0xffff), &sb, NULL));
    DISASM_TEST(sb, dis, "movabs rbx, 0x7fff0000\n");
    bf_comp_err e;
    CU_ASSERT_FALSE(set_reg(X86_EAX, ((i64)INT32_MAX) + 1, &sb, &e));
    CU_ASSERT_EQUAL(e.id, BF_ERR_CODE_TOO_LARGE);
    DISASM_TEST(sb, dis, "xor ebx, ebx\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_jump_instructions(void) {
    sized_buf sb = newbuf(27);
    sized_buf dis = newbuf(160);
    bf_comp_err e;
    sb_reserve(&sb, JUMP_SIZE);
    x86_jump_open(X86_EDI, 9, &sb, 0, &e);
    x86_jump_close(X86_EDI, -18, &sb, &e);
    x86_pad_loop_open(&sb);
    CU_ASSERT_EQUAL(sb.sz, 27);
    DISASM_TEST(
        sb,
        dis,
        "test byte ptr [edi], -0x1\n"
        "je 0x9\n"
        "test byte ptr [edi], -0x1\n"
        "jne -0x12\n"
        "ud2\n"
        "nop\nnop\nnop\nnop\nnop\nnop\nnop\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_small_imm(void) {
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(16);

    CU_ASSERT(add_reg(X86_ESI, 0x20, &sb, NULL));
    CU_ASSERT_EQUAL(sb.sz, 4);
    DISASM_TEST(sb, dis, "add esi, 0x20\n");

    CU_ASSERT(sub_reg(X86_ESI, 0x20, &sb, NULL));
    CU_ASSERT_EQUAL(sb.sz, 4);
    DISASM_TEST(sb, dis, "sub esi, 0x20\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_medium_imm(void) {
    sized_buf sb = newbuf(7);
    sized_buf dis = newbuf(24);

    CU_ASSERT(add_reg(X86_EDX, 0xdead, &sb, NULL));
    CU_ASSERT_EQUAL(sb.sz, 7);
    DISASM_TEST(sb, dis, "add edx, 0xdead\n");

    CU_ASSERT(sub_reg(X86_EDX, 0xbeef, &sb, NULL));
    CU_ASSERT_EQUAL(sb.sz, 7);
    DISASM_TEST(sb, dis, "sub edx, 0xbeef\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_large_imm(void) {
    sized_buf sb = newbuf(13);
    sized_buf dis = newbuf(40);

    CU_ASSERT(add_reg(X86_EBX, 0xdeadbeef, &sb, NULL));
    DISASM_TEST(sb, dis, "add ebx, 0xdeadbeef\n");

    CU_ASSERT(sub_reg(X86_EBX, 0xdeadbeef, &sb, NULL));
    DISASM_TEST(sb, dis, "sub ebx, 0xdeadbeef\n");

    bf_comp_err err;
    CU_ASSERT_FALSE(add_reg(X86_EAX, 0x2deadbeef, &sb, &err));
    DISASM_TEST(sb, dis, "add ebx, 0xdeadbeef\n");
    CU_ASSERT_EQUAL(err.msg.ref, VAL_TRUNCATED_WARNING);
    CU_ASSERT_EQUAL(err.id, BF_ERR_CODE_TOO_LARGE);

    CU_ASSERT_FALSE(sub_reg(X86_EAX, 0x2deadbeef, &sb, &err));
    DISASM_TEST(sb, dis, "sub ebx, 0xdeadbeef\n");
    CU_ASSERT_EQUAL(err.msg.ref, VAL_TRUNCATED_WARNING);
    CU_ASSERT_EQUAL(err.id, BF_ERR_CODE_TOO_LARGE);

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_byte(void) {
    sized_buf sb = newbuf(6);
    sized_buf dis = newbuf(56);

    x86_add_byte(X86_EDI, 0x23, &sb);
    x86_sub_byte(X86_EDI, 0x23, &sb);
    CU_ASSERT_EQUAL(sb.sz, 6);

    DISASM_TEST(
        sb,
        dis,
        "add byte ptr [edi], 0x23\n"
        "sub byte ptr [edi], 0x23\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_zero_byte(void) {
    sized_buf sb = newbuf(3);
    sized_buf dis = newbuf(32);

    x86_zero_byte(X86_EDX, &sb);
    DISASM_TEST(sb, dis, "mov byte ptr [edx], 0x0\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_jump_too_long(void) {
    bf_comp_err e;
    char dst[JUMP_SIZE];
    CU_ASSERT_FALSE(x86_jump_close(
        0,
        INT64_MAX,
        &(sized_buf){.buf = dst, .sz = 0, .capacity = JUMP_SIZE},
        &e
    ));
    CU_ASSERT_EQUAL(e.id, BF_ERR_JUMP_TOO_LONG);
}

static void test_inc_dec_is_32_bit(void) {
    sized_buf sb = newbuf(8);
    sized_buf dis = newbuf(56);
    inc_reg(X86_EAX, &sb);
    dec_reg(X86_EAX, &sb);
    x86_inc_byte(X86_EAX, &sb);
    x86_dec_byte(X86_EAX, &sb);

    DISASM_TEST(
        sb,
        dis,
        "inc eax\n"
        "dec eax\n"
        "inc byte ptr [eax]\n"
        "dec byte ptr [eax]\n"
    );

    free(sb.buf);
    free(dis.buf);
}

CU_pSuite register_i386_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, test_set_reg);
    ADD_TEST(suite, test_jump_instructions);
    ADD_TEST(suite, test_add_sub_small_imm);
    ADD_TEST(suite, test_add_sub_medium_imm);
    ADD_TEST(suite, test_add_sub_large_imm);
    ADD_TEST(suite, test_add_sub_byte);
    ADD_TEST(suite, test_zero_byte);
    ADD_TEST(suite, test_jump_too_long);
    ADD_TEST(suite, test_inc_dec_is_32_bit);
    return (suite);
}

#endif

#endif /* BFC_TARGET_I386 */
