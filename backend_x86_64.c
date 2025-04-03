/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file provides the arch_inter for the x86_64 architecture. */
/* internal */
#include "arch_inter.h"
#include "compat/elf.h"
#include "err.h"
#include "serialize.h"
#include "types.h"
#include "util.h"
#if BFC_TARGET_X86_64

enum X86_REGS {
    /* x86 32-bit register IDs */
    X86_EAX = 00,
    /* reserved for use in `reg_arith` only: `X86_ECX = 01,` */
    X86_EDX = 02,
    X86_EBX = 03,
    /* omit a few not used in eambfc */
    X86_ESI = 06,
    X86_EDI = 07,
    /* x86_64-only registers */
    X86_64_RAX = X86_EAX,
    /* reserved for use in `reg_arith` only: `X86_64_RCX = X86_ECX,` */
    X86_64_RDX = X86_EDX,
    /* omit a few not used in eambfc */
    X86_64_RBX = X86_EBX,
    X86_64_RSI = X86_ESI,
    X86_64_RDI = X86_EDI,
    /* omit extra numbered registers r10 through r15 added in x86_64 */
};

/* mark a series of bytes within a u8 array as being a single instruction,
 * mostly to prevent automated code formatting from splitting them up */
#define INSTRUCTION(...) __VA_ARGS__

/* nicer looking than having a bunch of integer literals inline to create the
 * needed space. */
#define IMM32_PADDING 0x00, 0x00, 0x00, 0x00
#define IMM64_PADDING 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

/* most common values for opcodes in add/sub instructions */
typedef enum { X64_OP_ADD = 0xc0, X64_OP_SUB = 0xe8 } arith_op;

#define JUMP_SIZE 9

/* TEST byte [reg], 0xff; Jcc|tttn offset */
static bool test_jcc(
    char tttn,
    u8 reg,
    i64 offset,
    char dst[restrict JUMP_SIZE],
    bf_comp_err *restrict err
) {
    if (offset > INT32_MAX || offset < INT32_MIN) {
        *err = basic_err(
            BF_ERR_JUMP_TOO_LONG,
            "offset is outside the range of possible 32-bit signed values"
        );
        return false;
    }
    memcpy(
        dst,
        (u8[5]){
            /* TEST byte [reg], 0xff */
            INSTRUCTION(0xf6, reg, 0xff),
            /* Jcc|tttn (must append jump offset) */
            INSTRUCTION(0x0f, 0x80 | tttn),
        },
        5
    );
    serialize32le(offset, &(dst[5]));
    return true;
}

static nonnull_args void reg_arith(
    u8 reg, u64 imm, arith_op op, sized_buf *restrict dst_buf
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
         * Instead, the approach  to use is first PUSH the value of a different
         * register, MOV the 64-bit immediate to that register, ADD/SUB that
         * register to the target register, then POP that temporary register, to
         * restore its original value. */
        /* the temporary register shouldn't be the target register, so use RCX,
         * which is a volatile register not used anywhere else in eambfc */
        const u8 TMP_REG = 1;
        u8 instr_bytes[] = {
            /* MOV TMP_REG, 0x0000000000000000 (will replace with imm64) */
            INSTRUCTION(0x48, 0xb8 + TMP_REG, IMM64_PADDING),
            /* (ADD||SUB) reg, tmp_reg */
            INSTRUCTION(0x48, op - 0xbf, 0xc0 + (TMP_REG << 3) + reg),
        };
        /* replace 0x0000000000000000 with imm64 */
        serialize64le(imm, &(instr_bytes[2]));
        append_obj(dst_buf, &instr_bytes, 13);
    }
}

/* now, the functions exposed through X86_64_INTER */
/* use the most efficient way to set a register to imm */
static nonnull_args void set_reg(u8 reg, i64 imm, sized_buf *restrict dst_buf) {
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
}

/* MOV rs, rd */
static nonnull_args void reg_copy(u8 dst, u8 src, sized_buf *restrict dst_buf) {
    append_obj(dst_buf, (u8[]){INSTRUCTION(0x89, 0xc0 + (src << 3) + dst)}, 2);
}

/* SYSCALL */
static nonnull_args void syscall(sized_buf *restrict dst_buf) {
    append_obj(dst_buf, (u8[]){INSTRUCTION(0x0f, 0x05)}, 2);
}

/* In this backend, `[` and `]` are both compiled to TEST (3 bytes), followed by
 * a Jcc instruction (6 bytes). When encountering a `[` instruction, fill 9
 * * bytes with NOP instructins to leave room for it. */
#define NOP 0x90

/* UD2; times 7 NOP */
static nonnull_args void pad_loop_open(sized_buf *restrict dst_buf) {
    u8 padding[9] = {0x0f, 0x0b, NOP, NOP, NOP, NOP, NOP, NOP, NOP};
    append_obj(dst_buf, &padding, 9);
}

/* TEST byte [reg], 0xff; JZ jmp_offset */
static nonnull_args bool jump_open(
    u8 reg,
    i64 offset,
    sized_buf *restrict dst_buf,
    size_t index,
    bf_comp_err *restrict err
) {
    /* Jcc with tttn=0b0100 is JZ or JE, so use 4 for tttn */
    return test_jcc(0x4, reg, offset, &dst_buf->buf[index], err);
}

/* TEST byte [reg], 0xff; JNZ jmp_offset */
static nonnull_args bool jump_close(
    u8 reg, i64 offset, sized_buf *restrict dst_buf, bf_comp_err *restrict err
) {
    /* Jcc with tttn=0b0101 is JNZ or JNE, so use 5 for tttn */
    return test_jcc(0x5, reg, offset, sb_reserve(dst_buf, JUMP_SIZE), err);
}

/* INC reg */
static nonnull_args void inc_reg(u8 reg, sized_buf *restrict dst_buf) {
    append_obj(dst_buf, (uchar[]){0x48, 0xff, 0xc0 | reg}, 3);
}

/* DEC reg */
static nonnull_args void dec_reg(u8 reg, sized_buf *restrict dst_buf) {
    append_obj(dst_buf, (uchar[]){0x48, 0xff, 0xc8 | reg}, 3);
}

/* INC byte [reg] */
static nonnull_args void inc_byte(u8 reg, sized_buf *restrict dst_buf) {
    append_obj(dst_buf, (uchar[]){0xfe, reg}, 2);
}

/* DEC byte [reg] */
static nonnull_args void dec_byte(u8 reg, sized_buf *restrict dst_buf) {
    append_obj(dst_buf, (uchar[]){0xfe, reg | 8}, 2);
}

static nonnull_args void add_reg(u8 reg, u64 imm, sized_buf *restrict dst_buf) {
    reg_arith(reg, imm, X64_OP_ADD, dst_buf);
}

static nonnull_args void sub_reg(u8 reg, u64 imm, sized_buf *restrict dst_buf) {
    reg_arith(reg, imm, X64_OP_SUB, dst_buf);
}

static nonnull_args void add_byte(
    u8 reg, u8 imm8, sized_buf *restrict dst_buf
) {
    /* ADD byte [reg], imm8 */
    append_obj(dst_buf, (u8[]){INSTRUCTION(0x80, reg, imm8)}, 3);
}

static nonnull_args void sub_byte(
    u8 reg, u8 imm8, sized_buf *restrict dst_buf
) {
    /* SUB byte [reg], imm8 */
    append_obj(dst_buf, (u8[]){INSTRUCTION(0x80, 0x28 + reg, imm8)}, 3);
}

static nonnull_args void zero_byte(u8 reg, sized_buf *restrict dst_buf) {
    /* MOV byte [reg], 0 */
    append_obj(dst_buf, (u8[]){INSTRUCTION(0xc6, reg, 0x00)}, 3);
}

const arch_inter X86_64_INTER = {
    .sc_read = 0,
    .sc_write = 1,
    .sc_exit = 60,
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
    .elf_arch = EM_X86_64,
    .elf_data = ELFDATA2LSB,
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
    sized_buf sb = newbuf(10);
    sized_buf dis = newbuf(32);

    set_reg(X86_EBX, 0, &sb);
    DISASM_TEST(sb, dis, "xor ebx, ebx\n");

    set_reg(X86_EBX, 128, &sb);
    DISASM_TEST(sb, dis, "mov ebx, 0x80\n");

    set_reg(X86_64_RBX, INT64_MAX - INT64_C(0xffff), &sb);
    DISASM_TEST(sb, dis, "movabs rbx, 0x7fffffffffff0000\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_jump_instructions(void) {
    sized_buf sb = newbuf(27);
    sized_buf dis = newbuf(160);
    bf_comp_err e;
    sb_reserve(&sb, JUMP_SIZE);
    jump_open(X86_64_RDI, 9, &sb, 0, &e);
    jump_close(X86_64_RDI, -18, &sb, &e);
    pad_loop_open(&sb);
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
    sized_buf sb = newbuf(4);
    sized_buf dis = newbuf(16);

    add_reg(X86_64_RSI, 0x20, &sb);
    CU_ASSERT_EQUAL(sb.sz, 4);
    DISASM_TEST(sb, dis, "add rsi, 0x20\n");

    sub_reg(X86_64_RSI, 0x20, &sb);
    CU_ASSERT_EQUAL(sb.sz, 4);
    DISASM_TEST(sb, dis, "sub rsi, 0x20\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_medium_imm(void) {
    sized_buf sb = newbuf(7);
    sized_buf dis = newbuf(24);

    add_reg(X86_64_RDX, 0xdead, &sb);
    CU_ASSERT_EQUAL(sb.sz, 7);
    DISASM_TEST(sb, dis, "add rdx, 0xdead\n");

    sub_reg(X86_64_RDX, 0xbeef, &sb);
    CU_ASSERT_EQUAL(sb.sz, 7);
    DISASM_TEST(sb, dis, "sub rdx, 0xbeef\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_large_imm(void) {
    sized_buf sb = newbuf(13);
    sized_buf dis = newbuf(40);

    add_reg(X86_64_RBX, 0xdeadbeef, &sb);
    DISASM_TEST(sb, dis, "movabs rcx, 0xdeadbeef\nadd rbx, rcx\n");

    sub_reg(X86_64_RBX, 0xdeadbeef, &sb);
    DISASM_TEST(sb, dis, "movabs rcx, 0xdeadbeef\nsub rbx, rcx\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_add_sub_byte(void) {
    sized_buf sb = newbuf(6);
    sized_buf dis = newbuf(56);

    add_byte(X86_64_RDI, 0x23, &sb);
    sub_byte(X86_64_RDI, 0x23, &sb);
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

static void test_zero_byte(void) {
    sized_buf sb = newbuf(3);
    sized_buf dis = newbuf(32);

    zero_byte(X86_64_RDX, &sb);
    DISASM_TEST(sb, dis, "mov byte ptr [rdx], 0x0\n");

    free(sb.buf);
    free(dis.buf);
}

static void test_jump_too_long(void) {
    bf_comp_err e;
    char dst[JUMP_SIZE];
    CU_ASSERT_FALSE(jump_close(
        0,
        INT64_MAX,
        &(sized_buf){.buf = dst, .sz = 0, .capacity = JUMP_SIZE},
        &e
    ));
    CU_ASSERT_EQUAL(e.id, BF_ERR_JUMP_TOO_LONG);
}

static void test_inc_dec_is_64_bit(void) {
    sized_buf sb = newbuf(10);
    sized_buf dis = newbuf(56);
    inc_reg(X86_64_RAX, &sb);
    dec_reg(X86_64_RAX, &sb);
    inc_byte(X86_64_RAX, &sb);
    dec_byte(X86_64_RAX, &sb);

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
    ADD_TEST(suite, test_zero_byte);
    ADD_TEST(suite, test_jump_too_long);
    ADD_TEST(suite, test_inc_dec_is_64_bit);
    return (suite);
}

#endif

#endif /* BFC_TARGET_X86_64 */
