/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * implements the steps to translate the code into an optimized `InstrSeq`
 * array. */

/* C99 */
#include <assert.h>
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
    InstrSeq *seq, size_t *len, size_t start, size_t elems
) {
    assert(start + elems <= *len);
    size_t end = start + elems;
    memmove(&seq[start], &seq[end], (*len - end) * sizeof(InstrSeq));
    *len -= elems;
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
                seq[index].source.end = seq[index + 1].source.end;
                break;
            case ISEQ_MOVE_RIGHT:
                /* check if the tags are the same. If so, simply add their
                 * values, otherwise, subtract the following value. */
                seq[index].count += seq[index + 1].count;
                seq[index].source.end = seq[index + 1].source.end;
                break;
            default:
                return;
        }
        if (seq[index].count) {
            /* drop the merged instruction */
            drop_instrs(seq, len, index + 1, 1);
        } else {
            drop_instrs(seq, len, index, 2);
            /* keep going with the instruction that was before the newly-merged
             * ones, to see if it can be merged into the next one.
             *
             * A bit confusing, so here's an example:
             * merging `[Move(1), Add(2), Add(-2), Move(-1)]` at index 1 would
             * result in `[Move(1), Move(-2)]`, so this would run the check
             * again and merge them into an empty program. */
            if (index == 0) return;
            --index;
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
                drop_instrs(seq, len, start, (i + 1) - start);
                if (start) recheck_mergable(seq, start - 1, len);
                recheck_mergable(seq, start, len);
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

        can_elim = may_have_nonzero || sequence[i].tag == ISEQ_LOOP_CLOSE;
    }
    return changed ? SUCCESS_CHANGED : SUCCESS_UNCHANGED;
}

static nonnull_args void join_set_cells(InstrSeq *instructions, size_t *len) {
    for (size_t i = 0; i + 2 < *len; ++i) {
        if (instructions[i].tag == ISEQ_LOOP_OPEN &&
            instructions[i + 1].tag == ISEQ_ADD &&
            instructions[i + 1].count % 2 &&
            instructions[i + 2].tag == ISEQ_LOOP_CLOSE) {
            instructions[i].tag = ISEQ_SET_CELL;
            if (i + 3 < *len && instructions[i + 3].tag == ISEQ_ADD) {
                instructions[i].count = instructions[i + 3].count;
                instructions[i].source.end = instructions[i + 3].source.end;
                drop_instrs(instructions, len, i + 1, 3);
            } else {
                instructions[i].count = 0;
                instructions[i].source.end = instructions[i + 2].source.end;
                drop_instrs(instructions, len, i + 1, 2);
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
                i64 signed_count = cast_i64(result->output.instrs[i].count);
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

static void display_iseq(const InstrSeq *iseq) {
#define BYTE_PARAM_VARIANT(enumerator) \
    fprintf( \
        stderr, \
        "(%zu:%zu) s:%zu, e:%zu, " #enumerator ", 0x%02hhx\n", \
        iseq->source.location.line, \
        iseq->source.location.col, \
        iseq->source.start, \
        iseq->source.end, \
        (u8)(iseq->count) \
    );
#define FULL_PARAM_VARIANT(enumerator) \
    fprintf( \
        stderr, \
        "(%zu:%zu) s:%zu, e:%zu, " #enumerator ", 0x%jx\n", \
        iseq->source.location.line, \
        iseq->source.location.col, \
        iseq->source.start, \
        iseq->source.end, \
        (umax)(iseq->count) \
    );
#define IGNORED_PARAM_VARIANT(enumerator) \
    fprintf( \
        stderr, \
        "(%zu:%zu) s:%zu, e:%zu, " #enumerator "\n", \
        iseq->source.location.line, \
        iseq->source.location.col, \
        iseq->source.start, \
        iseq->source.end \
    );

    switch ((int)(iseq->tag)) {
        case ISEQ_SET_CELL:
            BYTE_PARAM_VARIANT(ISEQ_SET_CELL);
            break;
        case ISEQ_ADD:
            BYTE_PARAM_VARIANT(ISEQ_ADD);
            break;
        case ISEQ_SUB:
            BYTE_PARAM_VARIANT(ISEQ_SUB);
            break;
        case ISEQ_MOVE_RIGHT:
            FULL_PARAM_VARIANT(ISEQ_MOVE_RIGHT);
            break;
        case ISEQ_MOVE_LEFT:
            FULL_PARAM_VARIANT(ISEQ_MOVE_LEFT);
            break;
        case ISEQ_READ:
            IGNORED_PARAM_VARIANT(ISEQ_READ);
            break;
        case ISEQ_WRITE:
            IGNORED_PARAM_VARIANT(ISEQ_WRITE);
            break;
        case ISEQ_LOOP_CLOSE:
            IGNORED_PARAM_VARIANT(ISEQ_LOOP_CLOSE);
            break;
        case ISEQ_LOOP_OPEN:
            IGNORED_PARAM_VARIANT(ISEQ_LOOP_OPEN);
            break;
        default:
            fprintf(
                stderr,
                "(%zu:%zu) s:%zu, e:%zu, unknown tag %d, 0x%jx\n",
                iseq->source.location.line,
                iseq->source.location.col,
                iseq->source.start,
                iseq->source.end,
                (int)(iseq->tag),
                (umax)(iseq->count)
            );
    }
}

static nonnull_args bool instr_eq(const InstrSeq *a, const InstrSeq *b) {
#define CHECK_FIELD(field) \
    do { \
        if (a->field != b->field) { \
            fprintf(stderr, "field %s mismatch\n", #field); \
            display_iseq(a); \
            display_iseq(b); \
            return false; \
        } \
    } while (false)

    CHECK_FIELD(source.location.col);
    CHECK_FIELD(source.location.line);
    CHECK_FIELD(source.start);
    CHECK_FIELD(source.end);
    CHECK_FIELD(tag);

    if (a->tag == ISEQ_ADD || a->tag == ISEQ_SUB || a->tag == ISEQ_SET_CELL) {
        if ((a->count & 0xff) != (b->count & 0xff)) {
            fputs("field count mismatch\n", stderr);
            display_iseq(a);
            display_iseq(b);
            return false;
        }
    }

    if (a->tag == ISEQ_MOVE_LEFT || a->tag == ISEQ_MOVE_RIGHT) {
        CHECK_FIELD(count);
    }

    return true;
#undef CHECK_FIELD
}

/* value way too large to come up in testing, so this can be used when
 * `instr_eq` should ignore the `count` field. */
#define IGNORED_COUNT UINT64_C(0xaaaaaaaaaaaaaaaa)

static void into_seq_test(void) {
    size_t len;
    InstrSeq *instructions = into_sequences("++->-<++[]-+,.", 14, &len);
    const InstrSeq expect[9] = {
        {{{1, 1}, 0, 2}, ISEQ_ADD, 1}, /*                       `++-` */
        {{{1, 4}, 3, 3}, ISEQ_MOVE_RIGHT, 1}, /*                `>`   */
        {{{1, 5}, 4, 4}, ISEQ_ADD, -1}, /*                      `-`   */
        {{{1, 6}, 5, 5}, ISEQ_MOVE_RIGHT, -1}, /*               `<`   */
        {{{1, 7}, 6, 7}, ISEQ_ADD, 2}, /*                       `++`  */
        {{{1, 9}, 8, 8}, ISEQ_LOOP_OPEN, IGNORED_COUNT}, /*     `[`   */
        {{{1, 10}, 9, 9}, ISEQ_LOOP_CLOSE, IGNORED_COUNT}, /*   `]`   */
        /* positions 1:11 and 1:12  cancel each other out       `-+`  */
        {{{1, 13}, 12, 12}, ISEQ_READ, IGNORED_COUNT}, /*       `,`   */
        {{{1, 14}, 13, 13}, ISEQ_WRITE, IGNORED_COUNT}, /*      `.`   */
    };

    if (len != 9) {
        CU_FAIL("Wrong number of sequences returned");
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        CU_ASSERT(instr_eq(&instructions[i], &expect[i]));
    }

    free(instructions);
}

/* used in some tests to pad out `InstrSeq` arrays */
#define PAD_SEQ (InstrSeq){.tag = -1}

#define IS(t, c) \
    (InstrSeq) { \
        .tag = t, .count = c \
    }

/* various checks of the `recheck_mergable` function's behavior */
static void combine_logic_test(void) {
    InstrSeq in_then_out[4] = {
        IS(ISEQ_MOVE_RIGHT, 1),
        IS(ISEQ_ADD, 32),
        IS(ISEQ_ADD, -32),
        IS(ISEQ_MOVE_RIGHT, -2),
    };
    InstrSeq merge_no_oob[4];
    memcpy(merge_no_oob, in_then_out, sizeof(InstrSeq[4]));

    size_t len = 4;
    /* after merging at index `1`, it should go on to merge the outer mergeable
     * pair */
    recheck_mergable(in_then_out, 1, &len);
    CU_ASSERT_EQUAL(len, 1);
    CU_ASSERT(instr_eq(&in_then_out[0], &IS(ISEQ_MOVE_RIGHT, -1)));

    len = 3;
    recheck_mergable(merge_no_oob, 1, &len);
    /* value should be left unchanged as the value it can merge with is
     * out-of-bounds. */
    CU_ASSERT(instr_eq(&merge_no_oob[0], &IS(ISEQ_MOVE_RIGHT, 1)));
    /* value shouldn't have been overwritten by the memmove */
    CU_ASSERT(instr_eq(&merge_no_oob[1], &IS(ISEQ_ADD, 0)));
    /* value shouldn't have been overwritten by the memmove or otherwise
     * modified. If it has, then there was a read beyond the length added to
     * the function. */
    CU_ASSERT(instr_eq(&merge_no_oob[2], &IS(ISEQ_ADD, -32)));
    /* value is out-of-bounds and shouldn't have been touched at all */
    CU_ASSERT(instr_eq(&merge_no_oob[3], &IS(ISEQ_MOVE_RIGHT, -2)));
    CU_ASSERT_EQUAL(len, 1);

    /* make sure that upper bytes within `count` are ignored when merging. */
    InstrSeq garbage_upper_bytes[2] = {
        {.tag = ISEQ_ADD, .count = 32 | 0xdeadbeef0000},
        {.tag = ISEQ_ADD, .count = (-32 & 0xff) | 0xbeefdead0000},
    };
    len = 2;
    recheck_mergable(garbage_upper_bytes, 0, &len);
    CU_ASSERT_EQUAL(len, 0);

    InstrSeq simply_mergable[8] = {
        IS(ISEQ_ADD, 64),
        IS(ISEQ_ADD, 32),
        IS(ISEQ_ADD, 16),
        IS(ISEQ_ADD, 8),
        IS(ISEQ_ADD, 4),
        IS(ISEQ_ADD, 2),
        IS(ISEQ_ADD, 1),
        PAD_SEQ,
    };

    len = 8;
    recheck_mergable(simply_mergable, 0, &len);
    CU_ASSERT_EQUAL(len, 2);
    CU_ASSERT(instr_eq(&simply_mergable[0], &IS(ISEQ_ADD, 127)));
    for (ufast_8 i = 1; i < 8; ++i) {
        CU_ASSERT_EQUAL(
            memcmp(&simply_mergable[i], &PAD_SEQ, sizeof(InstrSeq)), 0
        );
    }

    InstrSeq unmergable[4] = {
        PAD_SEQ, {.tag = ISEQ_READ}, {.tag = ISEQ_READ}, PAD_SEQ
    };
    len = 4;
    recheck_mergable(unmergable, 0, &len);
    CU_ASSERT_EQUAL(len, 4);
    CU_ASSERT(instr_eq(&unmergable[0], &PAD_SEQ));
    CU_ASSERT(instr_eq(&unmergable[1], &(InstrSeq){.tag = ISEQ_READ}));
    CU_ASSERT(instr_eq(&unmergable[2], &(InstrSeq){.tag = ISEQ_READ}));
    CU_ASSERT(instr_eq(&unmergable[3], &PAD_SEQ));
}

/* Test that the externally-visible `optimize_instructions` function generates
 * the right sequences for code designed to be almost entirely removable junk */
static void optimize_test(void) {
    const char *code =
        "[+++++]><+---+++-[-][,[-][+>-<]]-+[-+]-+[]+-[]\n"
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n"
        "[+-]>padding>+\n<\n"
        "----------------------------------------------------------------"
        "----------------------------------------------------------------"
        "----------------------------------------------------------------"
        "----------------------------------------------------------------"
        "[->+<][,.]\n+++\n";

    union opt_result res;
    if (!optimize_instructions(code, strlen(code) - 1, &res)) {
        CU_FAIL("Failed to generate InstrSeq from code");
        display_err(res.err);
        return;
    }

    const InstrSeq expected[9] = {
        /* code[308..=316] at line 3 column 5 is ">padding>" */
        {{.location = {3, 5}, .start = 308, .end = 316}, ISEQ_MOVE_RIGHT, 2},
        /* code[317] at line 3 column 14 is "+" */
        {{.location = {3, 14}, .start = 317, .end = 317}, ISEQ_ADD, 1},
        /* code[319] at line 4 column 1 is "<" */
        {{.location = {4, 1}, .start = 319, .end = 319}, ISEQ_MOVE_LEFT, 1},
        /* code[577] at line 5 column 257 is "[" */
        {{.location = {5, 257}, .start = 577, .end = 577}, ISEQ_LOOP_OPEN, 0},
        /* code[578] at line 5 column 258 is "-" */
        {{.location = {5, 258}, .start = 578, .end = 578}, ISEQ_SUB, 1},
        /* code[579] at line 5 column 259 is ">" */
        {{.location = {5, 259}, .start = 579, .end = 579}, ISEQ_MOVE_RIGHT, 1},
        /* code[580] at line 5 column 260 is "+" */
        {{.location = {5, 260}, .start = 580, .end = 580}, ISEQ_ADD, 1},
        /* code[581] at line 5 column 261 is "<" */
        {{.location = {5, 261}, .start = 581, .end = 581}, ISEQ_MOVE_LEFT, 1},
        /* code[582] at line 5 column 262 is "]" */
        {{.location = {5, 262}, .start = 582, .end = 582}, ISEQ_LOOP_CLOSE, 0},
    };

    if (res.output.len != 9) {
        CU_FAIL("Length mismatch");
        free(res.output.instrs);
        return;
    }
    for (ufast_8 i = 0; i < 9; ++i) {
        CU_ASSERT(instr_eq(&res.output.instrs[i], &expected[i]));
    }

    free(res.output.instrs);
}

static void set_cell_detected(void) {
    /* read before and after each loop to prevent them from being eliminated as
     * dead code. */
    const char *code = ",[-],[--],[---],[+++],[++],[+],";
    const InstrSeq expected[17] = {
        {.source = {{1, 1}, 0, 0}, .tag = ISEQ_READ},
        {.source = {{1, 2}, 1, 3}, .tag = ISEQ_SET_CELL, .count = 0},
        {.source = {{1, 5}, 4, 4}, .tag = ISEQ_READ},
        {.source = {{1, 6}, 5, 5}, .tag = ISEQ_LOOP_OPEN},
        {.source = {{1, 7}, 6, 7}, .tag = ISEQ_SUB, .count = 2},
        {.source = {{1, 9}, 8, 8}, .tag = ISEQ_LOOP_CLOSE},
        {.source = {{1, 10}, 9, 9}, .tag = ISEQ_READ},
        {.source = {{1, 11}, 10, 14}, .tag = ISEQ_SET_CELL, .count = 0},
        {.source = {{1, 16}, 15, 15}, .tag = ISEQ_READ},
        {.source = {{1, 17}, 16, 20}, .tag = ISEQ_SET_CELL, .count = 0},
        {.source = {{1, 22}, 21, 21}, .tag = ISEQ_READ},
        {.source = {{1, 23}, 22, 22}, .tag = ISEQ_LOOP_OPEN},
        {.source = {{1, 24}, 23, 24}, .tag = ISEQ_ADD, .count = 2},
        {.source = {{1, 26}, 25, 25}, .tag = ISEQ_LOOP_CLOSE},
        {.source = {{1, 27}, 26, 26}, .tag = ISEQ_READ},
        {.source = {{1, 28}, 27, 29}, .tag = ISEQ_SET_CELL, .count = 0},
        {.source = {{1, 31}, 30, 30}, .tag = ISEQ_READ},
    };
    union opt_result res;
    if (!optimize_instructions(code, strlen(code), &res)) {
        CU_FAIL("Failed to generate InstrSeq from code");
        display_err(res.err);
        return;
    }
    if (res.output.len != 17) {
        CU_FAIL("length mismatch");
        free(res.output.instrs);
        return;
    }
    for (ufast_8 i = 0; i < 17; ++i) {
        CU_ASSERT(instr_eq(&res.output.instrs[i], &expected[i]));
    }

    free(res.output.instrs);
}

CU_pSuite register_optimize_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, into_seq_test);
    ADD_TEST(suite, combine_logic_test);
    ADD_TEST(suite, optimize_test);
    ADD_TEST(suite, set_cell_detected);
    return suite;
}

#endif
