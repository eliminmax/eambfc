/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * implements the steps to translate the code into an optimized `InstrSeq`
 * array. */

/* C99 */
#include <stdlib.h>
#include <string.h>
/* internal */
#include <attributes.h>
#include <types.h>

#include "err.h"
#include "optimize.h"
#include "util.h"

static nonnull_args InstrSeq *into_sequences(
    const char *restrict code, size_t code_bytes, size_t *restrict seq_count
) {
    SizedBuf out_buf = (SizedBuf){NULL, 0, 0};
    SrcLoc location = {.line = 1, .col = 0};
    *seq_count = 0;

    InstrSeq *current_instr;
    char prev_raw = '\0';

    /* handle the boilerplate of incrementing (*seq_count), setting prev_raw,
     * and appending a new instruction with the provided TAG_VALUE */
#define APPEND_INSTR(RAW_BYTE, TAG_VAL) \
    ++(*seq_count); \
    prev_raw = RAW_BYTE; \
    current_instr = sb_reserve(&out_buf, sizeof(InstrSeq)); \
    current_instr->source = (struct loc_info){location, i, i}; \
    current_instr->tag = TAG_VAL

    for (size_t i = 0; i < code_bytes; ++i) {
        /* if it's not a UTF-8 continuation byte, increment the column number */
        if ((code[i] & 0xc0) != 0x80) location.col++;

        switch (code[i]) {
            case '.':
                APPEND_INSTR('.', ISEQ_WRITE);
                break;
            case ',':
                APPEND_INSTR(',', ISEQ_READ);
                break;
            case '[':
                APPEND_INSTR('[', ISEQ_LOOP_OPEN);
                break;
            case ']':
                APPEND_INSTR(']', ISEQ_LOOP_CLOSE);
                break;
            case '+':
                if (prev_raw == '+') {
                    current_instr->source.end = i;
                    current_instr->count++;
                    current_instr->count &= 0xff;
                    if (!current_instr->count) {
                        prev_raw = '\0';
                        out_buf.sz -= sizeof(InstrSeq);
                        --(*seq_count);
                    }
                } else {
                    APPEND_INSTR('+', ISEQ_ADD);
                    current_instr->count = 1;
                }
                break;
            case '>':
                if (prev_raw == '>') {
                    current_instr->source.end = i;
                    current_instr->count++;
                    if (!current_instr->count) {
                        prev_raw = '\0';
                        out_buf.sz -= sizeof(InstrSeq);
                        --(*seq_count);
                    }
                } else {
                    APPEND_INSTR('>', ISEQ_MOVE_RIGHT);
                    current_instr->count = 1;
                }
                break;
                /* cheating a bit by using ISEQ_ADD and ISEQ_MOVE_RIGHT with
                 * negative counts cast to unsigned instead of ISEQ_SUB and
                 * ISEQ_MOVE_LEFT with positive counts. The last step these are
                 * returned to functions outside of this translation unit is to
                 * convert them to the proper instructions. */
            case '-':
                if (prev_raw == '+') {
                    current_instr->source.end = i;
                    current_instr->count--;
                    current_instr->count &= 0xff;
                    if (!current_instr->count) {
                        prev_raw = '\0';
                        out_buf.sz -= sizeof(InstrSeq);
                        --(*seq_count);
                    }
                } else {
                    APPEND_INSTR('+', ISEQ_ADD);
                    current_instr->count = -1;
                }
                break;
            case '<':
                if (prev_raw == '>') {
                    current_instr->source.end = i;
                    current_instr->count--;
                    if (!current_instr->count) {
                        prev_raw = '\0';
                        out_buf.sz -= sizeof(InstrSeq);
                        --(*seq_count);
                    }
                } else {
                    APPEND_INSTR('>', ISEQ_MOVE_RIGHT);
                    current_instr->count = -1;
                }
                break;
            case '\n':
                location.col = 0;
                location.line++;
                break;
            default:
                break;
        }
    }

    return checked_realloc(out_buf.buf, out_buf.sz * sizeof(InstrSeq));
}

/* drop instruction sequences from `start` (inclusive) to `end` (exclusive),
 * updating the value of `*len` accordingly */
static nonnull_args void drop_instrs(
    InstrSeq *seq, size_t *len, size_t start, size_t end
) {
    memmove(&seq[start], &seq[end], (*len - end) * sizeof(InstrSeq));
    *len -= end - start;
}

/* check if any more instruction mergers can occur after the previous removal */
static nonnull_args void recheck_mergable(
    InstrSeq *seq, size_t index, size_t *len
) {
    while (index + 1 < *len && seq[index].tag == seq[index + 1].tag) {
        switch (seq[index].tag) {
            case ISEQ_ADD:
                /* check if the tags are the same. If so, simply add their
                 * values, otherwise, subtract the following value. */
                seq[index].count += seq[index + 1].count;
                seq[index].count &= 0xff;
                break;
            case ISEQ_MOVE_RIGHT:
                /* check if the tags are the same. If so, simply add their
                 * values, otherwise, subtract the following value. */
                seq[index].count += seq[index + 1].count;
                break;
            default:
                return;
        }
        if (seq[index].count) {
            /* drop the merged instruction */
            drop_instrs(seq, len, index, index + 1);
        } else {
            drop_instrs(seq, len, index, index + 2);
            if (index) recheck_mergable(seq, index - 1, len);
        }
    }
}

#undef MERGE_IMPL

static nonnull_args bool drain_loop(
    InstrSeq *seq, size_t start, size_t *len, BFCError *err
) {
    size_t nest_level = 1;
    for (size_t i = start + 1; i < *len; ++i) {
        if (seq[i].tag == ISEQ_LOOP_OPEN) {
            ++nest_level;
        } else if (seq[i].tag == ISEQ_LOOP_CLOSE) {
            --nest_level;
            if (!nest_level) {
                drop_instrs(seq, len, start, i + 1);
                recheck_mergable(seq, start ? start - 1 : 0, len);
                return true;
            }
        }
    }
    SrcLoc location = seq[start].source.location;
    free(seq);
    *err = (BFCError){
        .id = BF_ERR_UNMATCHED_OPEN,
        .msg.ref = "Could not optimize due to unmatched loop open",
        .location = location,
        .has_location = true,
    };
    return false;
}

/* find and remove loops that can be determined to always start with `0` already
 * in the current cell (meaning that they'll never run */
enum loop_removal_result {
    FAIL_UNMATCHED_OPEN = -1,
    SUCCESS_UNCHANGED = 0,
    SUCCESS_CHANGED = 1,
};

static nonnull_args enum loop_removal_result drop_dead_loops(
    InstrSeq *sequence, size_t *len, BFCError *err
) {
    bool may_have_nonzero = true;
    bool can_elim = true;
    bool changed = false;
    for (size_t i = 0; i < *len; ++i) {
        if (can_elim) {
            while (i < *len && sequence[i].tag == ISEQ_LOOP_OPEN) {
                changed = true;
                if (!drain_loop(sequence, i, len, err)) {
                    return FAIL_UNMATCHED_OPEN;
                }
            }
        }
        switch (sequence[i].tag) {
            case ISEQ_READ:
            case ISEQ_ADD:
            case ISEQ_SUB:
                may_have_nonzero = false;
                break;
            default:
                break;
        }

        can_elim = may_have_nonzero && sequence[i].tag == ISEQ_LOOP_CLOSE;
    }
    return changed ? SUCCESS_CHANGED : SUCCESS_UNCHANGED;
}

static nonnull_args void join_set_cells(InstrSeq *instructions, size_t *len) {
    for (size_t i = 0; i + 2 < *len; ++i) {
        if (instructions[i].tag == ISEQ_LOOP_OPEN &&
            instructions[i + 1].tag == ISEQ_ADD &&
            instructions[i + 1].count % 2 &&
            instructions[i + 2].tag == ISEQ_LOOP_CLOSE) {
            drop_instrs(instructions, len, i + 1, i + 3);
            instructions[i].tag = ISEQ_SET_CELL;
            if (i + 1 < *len && instructions[i + 1].tag == ISEQ_ADD) {
                instructions[i].count = instructions[i + 1].count;
                drop_instrs(instructions, len, i + 1, i + 2);
            }
        }
    }
}

nonnull_args bool optimize_instructions(
    const char *restrict code, size_t size, union opt_result *restrict result
) {
    result->output.instrs = into_sequences(code, size, &result->output.len);

    enum loop_removal_result res;
    while (
        (res = drop_dead_loops(
             result->output.instrs, &result->output.len, &result->err
         ))
    ) {
        if (res == FAIL_UNMATCHED_OPEN) return false;
    }
    join_set_cells(result->output.instrs, &result->output.len);
    for (size_t i = result->output.len; i; --i) {
        if (result->output.instrs[i - 1].tag == ISEQ_LOOP_CLOSE ||
            result->output.instrs[i - 1].tag == ISEQ_READ ||
            result->output.instrs[i - 1].tag == ISEQ_WRITE) {
            break;
        }
        --result->output.len;
    }
    for (size_t i = 0; i < result->output.len; ++i) {
        switch (result->output.instrs[i].tag) {
            case ISEQ_ADD: {
                i8 signed_count =
                    sign_extend(result->output.instrs[i].count, 8);
                if (signed_count < 0) {
                    result->output.instrs[i].count = -signed_count;
                    result->output.instrs[i].tag = ISEQ_SUB;
                }
            } break;
            case ISEQ_MOVE_RIGHT: {
                /* because `i64` is technically `int_least64_t`, which is
                 * possibly more than 64 bits, this check still requires sign
                 * extension */
                i64 signed_count =
                    sign_extend(result->output.instrs[i].count, 64);
                if (signed_count < 0) {
                    result->output.instrs[i].count = -signed_count;
                    result->output.instrs[i].tag = ISEQ_MOVE_LEFT;
                }
            } break;
            default:
                break;
        }
    }
    return true;
}

#ifdef BFC_TEST
/* internal */
#include "unit_test.h"

CU_pSuite register_optimize_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    return suite;
}

#endif
