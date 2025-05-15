/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file provides the ArchInter for the x86_64 architecture. */
/* internal */
#include <config.h>
#include <types.h>

#include "arch_inter.h"
#include "err.h"
#include "serialize.h"
#include "util.h"
#include "x86_common.h"

#if BFC_TARGET_X86_64

/* nicer looking than having a bunch of integer literals inline to create the
 * needed space. */
#define IMM64_PADDING 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

static nonnull_args void reg_arith(
    u8 reg, u64 imm, X86ArithOp op, SizedBuf *restrict dst_buf
) {
    if (imm == 0) {
        return;
    } else if (imm <= INT8_MAX) {
        /* ADD/SUB reg, byte imm */
        append_obj(dst_buf, (uchar[]){0x48, 0x83, op + reg, imm}, 4);
    } else if (imm <= INT32_MAX) {
        /* ADD/SUB reg, imm */
        uchar i_bytes[7] = {0x48, 0x81, op + reg};
        serialize32le(imm, &(i_bytes[3]));
        append_obj(dst_buf, &i_bytes, 7);
    } else {
        /* There are no instructions to add or subtract a 64-bit immediate.
         * Instead, the approach  to use is first store the value of in a
         * different register, then ADD/SUB that register to the target */
        /* the temporary register shouldn't be the target register, so use RCX,
         * which is a volatile register which is not used anywhere else in this
         * backend. */
        u8 instr_bytes[] = {
            /* MOV RCX, 0x0000000000000000 (will replace with imm64) */
            INSTRUCTION(0x48, 0xb8 + X86_64_RCX, IMM64_PADDING),
            /* (ADD||SUB) reg, tmp_reg */
            INSTRUCTION(0x48, op - 0xbf, 0xc0 + (X86_64_RCX << 3) + reg),
        };
        /* replace 0x0000000000000000 with imm64 */
        serialize64le(imm, &(instr_bytes[2]));
        append_obj(dst_buf, &instr_bytes, 13);
    }
}

/* now, the functions exposed through X86_64_INTER */
/* use the most efficient way to set a register to imm */
static nonnull_arg(3) bool set_reg(
    u8 reg, i64 imm, SizedBuf *restrict dst_buf, BFCError *restrict err
) {
    (void)err;
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
        /* MOV reg, imm64 */
        u8 instr_bytes[10] = {INSTRUCTION(0x48, 0xb8 | reg, IMM64_PADDING)};
        serialize64le(imm, &(instr_bytes[2]));
        append_obj(dst_buf, &instr_bytes, 10);
    }
    return true;
}

/* SYSCALL */
static nonnull_args void syscall(SizedBuf *restrict dst_buf, u32 sc_num) {
    set_reg(X86_EAX, sc_num, dst_buf, NULL);
    append_obj(dst_buf, (u8[]){INSTRUCTION(0x0f, 0x05)}, 2);
}

/* INC reg */
static nonnull_args void inc_reg(u8 reg, SizedBuf *restrict dst_buf) {
    append_obj(dst_buf, (uchar[]){0x48, 0xff, 0xc0 | reg}, 3);
}

/* DEC reg */
static nonnull_args void dec_reg(u8 reg, SizedBuf *restrict dst_buf) {
    append_obj(dst_buf, (uchar[]){0x48, 0xff, 0xc8 | reg}, 3);
}

static nonnull_arg(3) bool add_reg(
    u8 reg, u64 imm, SizedBuf *restrict dst_buf, BFCError *restrict err
) {
    (void)err;
    reg_arith(reg, imm, X64_OP_ADD, dst_buf);
    return true;
}

static nonnull_arg(3) bool sub_reg(
    u8 reg, u64 imm, SizedBuf *restrict dst_buf, BFCError *restrict err
) {
    (void)err;
    reg_arith(reg, imm, X64_OP_SUB, dst_buf);
    return true;
}

const ArchInter X86_64_INTER = {
    .sc_read = 0,
    .sc_write = 1,
    .sc_exit = 60,
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
    .set_byte = x86_set_byte,
    .flags = 0 /* no flags are defined for this architecture */,
    .elf_arch = 62 /* EM_X86_64 */,
    .elf_data = BYTEORDER_LSB,
    .addr_size = PTRSIZE_64,
    .reg_sc_num = X86_64_RAX,
    .reg_arg1 = X86_64_RDI,
    .reg_arg2 = X86_64_RSI,
    .reg_arg3 = X86_64_RDX,
    .reg_bf_ptr = X86_64_RBX,
};

#ifdef BFC_TEST

#include "unit_test.h"
#define REF X86_64_DIS

static void test_set_reg(void) {
    SizedBuf sb = newbuf(10);
    SizedBuf dis = newbuf(32);

    CU_ASSERT(set_reg(X86_EBX, 0, &sb, NULL));
    DISASM_TEST(sb, dis, "xor ebx, ebx\n");

    CU_ASSERT(set_reg(X86_EBX, 128, &sb, NULL));
    DISASM_TEST(sb, dis, "mov ebx, 0x80\n");

    CU_ASSERT(set_reg(X86_64_RBX, INT64_MAX - INT64_C(0xffff), &sb, NULL));
    DISASM_TEST(sb, dis, "movabs rbx, 0x7fffffffffff0000\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_jump_instructions(void) {
    SizedBuf sb = newbuf(27);
    SizedBuf dis = newbuf(160);
    BFCError e;
    sb_reserve(&sb, JUMP_SIZE);
    x86_jump_open(X86_64_RDI, 9, &sb, 0, &e);
    x86_jump_close(X86_64_RDI, -18, &sb, &e);
    x86_pad_loop_open(&sb);
    CU_ASSERT_EQUAL(sb.sz, 27);
    DISASM_TEST(
        sb,
        dis,
        "test byte ptr [rdi], -0x1\n"
        "je 0x9\n"
        "test byte ptr [rdi], -0x1\n"
        "jne -0x12\n"
        "ud2\n"
        "nop\nnop\nnop\nnop\nnop\nnop\nnop\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_small_imm(void) {
    SizedBuf sb = newbuf(4);
    SizedBuf dis = newbuf(16);

    CU_ASSERT(add_reg(X86_64_RSI, 0x20, &sb, NULL));
    CU_ASSERT_EQUAL(sb.sz, 4);
    DISASM_TEST(sb, dis, "add rsi, 0x20\n");

    CU_ASSERT(sub_reg(X86_64_RSI, 0x20, &sb, NULL));
    CU_ASSERT_EQUAL(sb.sz, 4);
    DISASM_TEST(sb, dis, "sub rsi, 0x20\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_medium_imm(void) {
    SizedBuf sb = newbuf(7);
    SizedBuf dis = newbuf(24);

    CU_ASSERT(add_reg(X86_64_RDX, 0xdead, &sb, NULL));
    CU_ASSERT_EQUAL(sb.sz, 7);
    DISASM_TEST(sb, dis, "add rdx, 0xdead\n");

    CU_ASSERT(sub_reg(X86_64_RDX, 0xbeef, &sb, NULL));
    CU_ASSERT_EQUAL(sb.sz, 7);
    DISASM_TEST(sb, dis, "sub rdx, 0xbeef\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_large_imm(void) {
    SizedBuf sb = newbuf(13);
    SizedBuf dis = newbuf(40);

    CU_ASSERT(add_reg(X86_64_RBX, 0xdeadbeef, &sb, NULL));
    DISASM_TEST(sb, dis, "movabs rcx, 0xdeadbeef\nadd rbx, rcx\n");

    CU_ASSERT(sub_reg(X86_64_RBX, 0xdeadbeef, &sb, NULL));
    DISASM_TEST(sb, dis, "movabs rcx, 0xdeadbeef\nsub rbx, rcx\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_byte(void) {
    SizedBuf sb = newbuf(6);
    SizedBuf dis = newbuf(56);

    x86_add_byte(X86_64_RDI, 0x23, &sb);
    x86_sub_byte(X86_64_RDI, 0x23, &sb);
    CU_ASSERT_EQUAL(sb.sz, 6);

    DISASM_TEST(
        sb,
        dis,
        "add byte ptr [rdi], 0x23\n"
        "sub byte ptr [rdi], 0x23\n"
    );

    free(sb.buf);
    free(dis.buf);
}

static void test_set_cell(void) {
    SizedBuf sb = newbuf(3);
    SizedBuf dis = newbuf(32);

    x86_set_byte(X86_64_RDX, 0, &sb);
    DISASM_TEST(sb, dis, "mov byte ptr [rdx], 0x0\n");
    x86_set_byte(X86_64_RSI, 0x40, &sb);
    DISASM_TEST(sb, dis, "mov byte ptr [rsi], 0x40\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_jump_too_long(void) {
    BFCError e;
    char dst[JUMP_SIZE];
    CU_ASSERT_FALSE(x86_jump_close(
        0,
        INT64_MAX,
        &(SizedBuf){.buf = dst, .sz = 0, .capacity = JUMP_SIZE},
        &e
    ));
    CU_ASSERT_EQUAL(e.id, BF_ERR_JUMP_TOO_LONG);
}

static void test_inc_dec_is_64_bit(void) {
    SizedBuf sb = newbuf(10);
    SizedBuf dis = newbuf(56);
    inc_reg(X86_64_RAX, &sb);
    dec_reg(X86_64_RAX, &sb);
    x86_inc_byte(X86_64_RAX, &sb);
    x86_dec_byte(X86_64_RAX, &sb);

    DISASM_TEST(
        sb,
        dis,
        "inc rax\n"
        "dec rax\n"
        "inc byte ptr [rax]\n"
        "dec byte ptr [rax]\n"
    );

    free(sb.buf);
    free(dis.buf);
}

CU_pSuite register_x86_64_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, test_set_reg);
    ADD_TEST(suite, test_jump_instructions);
    ADD_TEST(suite, test_add_sub_small_imm);
    ADD_TEST(suite, test_add_sub_medium_imm);
    ADD_TEST(suite, test_add_sub_large_imm);
    ADD_TEST(suite, test_add_sub_byte);
    ADD_TEST(suite, test_set_cell);
    ADD_TEST(suite, test_jump_too_long);
    ADD_TEST(suite, test_inc_dec_is_64_bit);
    return (suite);
}

#endif

#endif /* BFC_TARGET_X86_64 */
