/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

/* C99 */
#include <string.h>
/* internal */
#include <config.h>

#include "err.h"
#include "serialize.h"
#include "util.h"
#include "x86_common.h"

/* MOV rs, rd */
nonnull_args void x86_reg_copy(u8 dst, u8 src, SizedBuf *restrict dst_buf) {
    append_obj(dst_buf, (u8[]){INSTRUCTION(0x89, 0xc0 + (src << 3) + dst)}, 2);
}

/* `TEST byte [reg], 0xff; Jcc|tttn offset` */
static bool test_jcc(
    char tttn,
    u8 reg,
    i64 offset,
    char dst[restrict JUMP_SIZE],
    BFCError *restrict err
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

nonnull_args bool x86_jump_open(
    u8 reg,
    i64 offset,
    SizedBuf *restrict dst_buf,
    size_t index,
    BFCError *restrict err
) {
    /* Jcc with tttn=0b0100 is JZ or JE, so use 4 for tttn */
    return test_jcc(0x4, reg, offset, (char *)dst_buf->buf + index, err);
}

/* TEST byte [reg], 0xff; JNZ jmp_offset */
nonnull_args bool x86_jump_close(
    u8 reg, i64 offset, SizedBuf *restrict dst_buf, BFCError *restrict err
) {
    /* Jcc with tttn=0b0101 is JNZ or JNE, so use 5 for tttn */
    return test_jcc(0x5, reg, offset, sb_reserve(dst_buf, JUMP_SIZE), err);
}

/* In x86 backends, `[` and `]` are both compiled to TEST (3 bytes), followed by
 * a Jcc instruction (6 bytes). When encountering a `[` instruction, add a trap
 * instruction then pad with NOP instructions to leave room for those
 * instructions to be filled in once the corresponding `]` is reached. */
#define NOP 0x90

/* UD2; times 7 NOP */
nonnull_args void x86_pad_loop_open(SizedBuf *restrict dst_buf) {
    u8 padding[9] = {0x0f, 0x0b, NOP, NOP, NOP, NOP, NOP, NOP, NOP};
    append_obj(dst_buf, &padding, 9);
}

nonnull_args void x86_add_byte(u8 reg, u8 imm8, SizedBuf *restrict dst_buf) {
    /* ADD byte [reg], imm8 */
    append_obj(dst_buf, (u8[]){INSTRUCTION(0x80, reg, imm8)}, 3);
}

nonnull_args void x86_sub_byte(u8 reg, u8 imm8, SizedBuf *restrict dst_buf) {
    /* SUB byte [reg], imm8 */
    append_obj(dst_buf, (u8[]){INSTRUCTION(0x80, 0x28 + reg, imm8)}, 3);
}

nonnull_args void x86_zero_byte(u8 reg, SizedBuf *restrict dst_buf) {
    /* MOV byte [reg], 0 */
    append_obj(dst_buf, (u8[]){INSTRUCTION(0xc6, reg, 0x00)}, 3);
}

/* INC byte [reg] */
nonnull_args void x86_inc_byte(u8 reg, SizedBuf *restrict dst_buf) {
    append_obj(dst_buf, (uchar[]){0xfe, reg}, 2);
}

/* DEC byte [reg] */
nonnull_args void x86_dec_byte(u8 reg, SizedBuf *restrict dst_buf) {
    append_obj(dst_buf, (uchar[]){0xfe, reg | 8}, 2);
}
