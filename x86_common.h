/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

/* internal */
#include <attributes.h>
#include <config.h>
#include <types.h>

#include "err.h"
#ifndef BFC_X86_COMMON
#define BFC_X86_COMMON 1

#if BFC_TARGET_X86_64 || BFC_TARGET_I386

enum X86_REGS {
    /* x86 32-bit register IDs */
    X86_EAX = 00,
    /* reserved for use in `reg_arith` only: `X86_ECX = 01,` */
    X86_EDX = 02,
    X86_EBX = 03,
    /* omit a few not used in eambfc */
    X86_ESI = 06,
    X86_EDI = 07,
#if BFC_TARGET_X86_64
    /* x86_64-only registers */
    X86_64_RAX = X86_EAX,
    /* reserved for use in `reg_arith` only: `X86_64_RCX = X86_ECX,` */
    X86_64_RDX = X86_EDX,
    /* omit a few not used in eambfc */
    X86_64_RBX = X86_EBX,
    X86_64_RSI = X86_ESI,
    X86_64_RDI = X86_EDI,
/* omit extra numbered registers r10 through r15 added in x86_64 */
#endif /* BFC_TARGET_X86_64 */
};

/* mark a series of bytes within a u8 array as being a single instruction,
 * mostly to prevent automated code formatting from splitting them up */
#define INSTRUCTION(...) __VA_ARGS__

/* nicer looking than having a bunch of integer literals inline to create the
 * needed space. */
#define IMM32_PADDING 0x00, 0x00, 0x00, 0x00

/* most common values for opcodes in add/sub instructions */
typedef enum { X64_OP_ADD = 0xc0, X64_OP_SUB = 0xe8 } arith_op;

#define JUMP_SIZE 9

/* backend functions common to both the x86_64 and i386 backends */
/* compile the `[` bf instruction */
nonnull_args bool x86_jump_open(
    u8 reg,
    i64 offset,
    sized_buf *restrict dst_buf,
    size_t index,
    bf_comp_err *restrict err
);

/* compile the `]` bf instruction */
nonnull_args bool x86_jump_close(
    u8 reg, i64 offset, sized_buf *restrict dst_buf, bf_comp_err *restrict err
);

/* reserve space for the `[` bf instruction */
nonnull_args void x86_pad_loop_open(sized_buf *restrict dst_buf);

/* zero out a byte */
nonnull_args void x86_zero_byte(u8 reg, sized_buf *restrict dst_buf);

/* increment a byte */
nonnull_args void x86_inc_byte(u8 reg, sized_buf *restrict dst_buf);
/* decrement a byte */
nonnull_args void x86_dec_byte(u8 reg, sized_buf *restrict dst_buf);
/* subtract an immediate from a byte */
nonnull_args void x86_add_byte(u8 reg, u8 imm8, sized_buf *restrict dst_buf);
/* subtract an immediate from a byte */
nonnull_args void x86_sub_byte(u8 reg, u8 imm8, sized_buf *restrict dst_buf);

#endif /* BFC_TARGET_X86_64 || BFC_TARGET_I386 */
#endif /* BFC_X86_COMMON */
