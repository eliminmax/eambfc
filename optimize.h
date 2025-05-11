/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Provides a function that returns EAMBFC IR from an open FD */

#ifndef BFC_OPTIMIZE_H
#define BFC_OPTIMIZE_H 1
/* internal */
#include <attributes.h>
#include <types.h>

#include "err.h"

/* A sequence of instructions that can be compiled all at once */
typedef struct instr_seq {
    /* info about where the instruction came from */
    struct loc_info {
        /* source code location (for error reporting) */
        SrcLoc location;
        /* byte index in the file of the start of the sequence */
        size_t start;
        /* byte index in the file of the end of the sequence */
        size_t end;
    } source;

    /* tag uesed to indicate effect of this sequence. */
    enum instr_seq_tag {
        /* set a cell to `data.byte.value` */
        ISEQ_SET_CELL,
        /* `[` instruction */
        ISEQ_LOOP_OPEN,
        /* `]` instruction */
        ISEQ_LOOP_CLOSE,
        /* `,` instruction */
        ISEQ_READ,
        /* `.` instruction */
        ISEQ_WRITE,
        /* `+` and `-` can combine, as can `<` and `>`, so combinable
         * instruction pairs have the same absolute value. */
        /* data[0] will be */
        /* A sequence of instructions equivalent in effect to `+` repeated
         * `count & 0xff` times. */
        ISEQ_ADD,
        /* A sequence of instructions equivalent in effect to `>` repeated
         * `count & 0xff` times. */
        ISEQ_MOVE_RIGHT,
        /* A sequence of instructions equivalent in effect to `-` repeated
         * `count` times. */
        ISEQ_SUB,
        /* A sequence of instructions equivalent in effect to `<` repeated
         * `count` times. */
        ISEQ_MOVE_LEFT,
    } tag;

    /* Data that some sequence types need. Meaning depends on `tag`, and is
     * explained in docstrings for the different variants. */
    u64 count;

} InstrSeq;

union opt_result {
    struct {
        size_t len;
        InstrSeq *instrs;
    } output;

    BFCError *err;
};

nonnull_args bool optimize_instructions(
    const char *restrict code, size_t size, union opt_result *result
);

/* filter out all non-BF bytes, and anything that is trivially determined to be
 * dead code, or code with no effect (e.g. "+-" or "<>"), and replace "[-]" and
 * "[+]" with "@".
 *
 * "in_name" is the source filename, and is used only to generate error
 * messages. */
nonnull_args bool filter_dead(SizedBuf *bf_code, const char *in_name);
#endif /* BFC_OPTIMIZE_H */
