/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */
#include "serialize.h"
#include "types.h"
#include "util.h"

/* #if BFC_TARGET_RISCV64 */

/* SPDX-SnippetCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only AND Apache-2.0 WITH LLVM-exception
 *
 * SPDX-SnippetCopyrightText: 2021 Alexander Pivovarov
 * SPDX-SnippetCopyrightText: 2021 Ben Shi
 * SPDX-SnippetCopyrightText: 2021 Craig Topper
 * SPDX-SnippetCopyrightText: 2021 Jim Lin
 * SPDX-SnippetCopyrightText: 2020 Simon Pilgrim
 * SPDX-SnippetCopyrightText: 2018 - 2019 Alex Bradbury
 * SPDX-SnippetCopyrightText: 2019 Chandler Carruth
 * SPDX-SnippetCopyrightText: 2019 Sam Elliott
 *
 * modification copyright 2025 Eli Array Minkoff
 *
 * This function is a C rewrite of a Rust port of the LLVM logic for
 * resolving the `li` (load-immediate) pseudo-instruction
 *
 * And yes, I did have to go through the git commit history of the original file
 * to find everyone who had contributed as of 2022, because I didn't want to
 * just go "Copyright 2018-2022 LLVM Contributors"
 * and call it a day - I believe firmly in giving credit where credit is do. */
void encode_li(sized_buf *code_buf, u8 reg, i64 val) {
    u8 i_bytes[4];
    u32 lo12 = sign_extend(val, 12);
    if (bit_fits_s(val, 32)) {
        i32 hi20 = sign_extend(((u64)val + 0x800) >> 12, 20);
        if (hi20 && bit_fits_s(hi20, 6)) {
            u16 instr =
                0x6001 | (((hi20 & 0x20) | reg) << 7) | ((hi20 & 0x1f) << 2);
            serialize16le(instr, &i_bytes);
            append_obj(code_buf, &i_bytes, 2);
        } else if (hi20) {
            serialize32le((hi20 << 12) | (reg << 7) | 0x37, &i_bytes);
            append_obj(code_buf, &i_bytes, 4);
        }
        if (lo12 || !hi20) {
            if (bit_fits_s((i32)lo12, 6)) {
                /* if n == 0: `C.LI reg, lo6`
                 * else: `ADDIW reg, reg, lo6` */
                u16 instr = (hi20 ? 0x2001 : 0x4001) |
                            (((lo12 & 0x20) | reg) << 7) | ((lo12 & 0x1f) << 2);
                serialize16le(instr, &i_bytes);
                append_obj(code_buf, &i_bytes, 2);
            } else {
                /* if n != 0: `ADDIW reg, reg, lo12`
                 * else `ADDI reg, zero, lo12` */
                u32 opcode = hi20 ? 0x1b : 0x13;
                u32 rs1 = hi20 ? (((u32)reg) << 15) : 0;
                u32 instr = (lo12 << 20) | rs1 | (((u32)reg) << 7) | opcode;
                serialize32le(instr, &i_bytes);
                append_obj(code_buf, &i_bytes, 4);
            }
        }
        return;
    }
    i64 hi52 = ((u64)val + 0x800) >> 12;
    uint shift = trailing_0s(hi52) + 12;
    hi52 = sign_extend((u64)hi52 >> (shift - 12), 64 - shift);

    // If the remaining bits don't fit in 12 bits, we might be able to reduce
    // the shift amount in order to use LUI which will zero the lower 12 bits.
    if (shift > 12 && (!bit_fits_s(hi52, 12)) &&
        bit_fits_s(((u64)hi52 << 12), 32)) {
        shift -= 12;
        hi52 = ((u64)hi52) << 12;
    }
    encode_li(code_buf, reg, hi52);
    if (shift) {
        /* C.SLLI reg, shift_amount */
        u16 instr = (((shift & 0x20) | reg) << 7) | ((shift & 0x1f) << 2) | 2;
        serialize16le(instr, &i_bytes);
        append_obj(code_buf, i_bytes, 2);
    }
    if (lo12 && bit_fits_s((i16)lo12, 6)) {
        /* C.ADDI reg, reg, lo12 */
        u16 instr =
            0x0001 | (((lo12 & 0x20) | reg) << 7) | ((lo12 & 0x1f) << 2);
        serialize16le(instr, &i_bytes);
        append_obj(code_buf, i_bytes, 2);
    } else if (lo12) {
        /* ADDI, reg, reg, lo12 */
        u32 instr = (lo12 << 20) | ((u32)reg << 15) | ((u32)reg << 7) | 0x13;
        serialize32le(instr, &i_bytes);
        append_obj(code_buf, i_bytes, 4);
    }
}

/* #endif /1* BFC_TARGET_RISCV64 *1/ */
