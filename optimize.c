/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * implements the filter_dead function, which filters non-bf bytes and dead code
 * and replaces `[-]` and `[+]` sequences with `@`. */

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
    while (index + 1 == *len &&
           (seq[index].tag == ISEQ_ADD || seq[index].tag == ISEQ_MOVE_RIGHT) &&
           seq[index].tag == seq[index + 1].tag) {
        switch (seq[index].tag) {
            case ISEQ_ADD:
            case ISEQ_SUB:
                /* check if the tags are the same. If so, simply add their
                 * values, otherwise, subtract the following value. */
                if (seq[index].tag == seq[index + 1].tag) {
                    seq[index].count += seq[index + 1].count;
                } else {
                    seq[index].count -= seq[index + 1].count;
                }
                seq[index].count &= 0xff;
                break;
            case ISEQ_MOVE_RIGHT:
            case ISEQ_MOVE_LEFT:
                /* check if the tags are the same. If so, simply add their
                 * values, otherwise, subtract the following value. */
                if (seq[index].tag == seq[index + 1].tag) {
                    seq[index].count += seq[index + 1].count;
                } else {
                    seq[index].count -= seq[index + 1].count;
                }
                break;
            default:
                return;
        }
        if (seq[index].count) {
            /* drop the merged instruction */
            drop_instrs(seq, len, index, index + 1);
        } else {
            drop_instrs(seq, len, index, index + 2);
            /* check if that lets the previous instruction merge */
            if (index) --index;
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

/* filter out the non-bf characters from code->buf */
static nonnull_args void filter_non_bf(SizedBuf *code) {
    SizedBuf tmp = newbuf(code->sz);
    char instr;
    for (size_t i = 0; i < code->sz; i++) {
        switch (instr = ((char *)code->buf)[i]) {
            case '[':
            case '-':
            case '.':
            case '<':
            case '>':
            case ',':
            case '+':
            case ']':
                append_obj(&tmp, &instr, 1);
                break;
        }
    }
    instr = '\0';
    /* null terminate it */
    append_obj(&tmp, &instr, 1);
    code->sz = 0;
    append_obj(code, tmp.buf, tmp.sz);
    free(tmp.buf);
}

static void instr_err(
    BfErrorId id, const char *msg, char instr, const char *in_name
) {
    display_err((BFCError){
        .id = id,
        .msg.ref = msg,
        .instr = instr,
        .file = in_name,
        .has_instr = true,
    });
}

/* A function that skips past a matching ].
 * loop_start is a pointer to the [ at the start of the loop */
static const nonnull_args char *find_loop_end(
    const char *loop_start, const char *in_name
) {
    uint nest_level = 1;
    const char *p = loop_start;
    while (*(++p)) {
        if (*p == '[') {
            nest_level++;
        } else if (*p == ']') {
            if (--nest_level == 0) return p + 1;
        }
    }
    /* The above loop only terminates if an '[' was unmatched. */
    instr_err(
        BF_ERR_UNMATCHED_OPEN,
        "Could not optimize due to unmatched '['",
        '[',
        in_name
    );
    return NULL;
}

/* return true if the loops are balanced, false otherwise */
static nonnull_args bool loops_match(const char *code, const char *in_name) {
    const char *open_p = strchr(code, '[');
    const char *close_p = strchr(code, ']');
    /* if none are found, it's fine. */
    if ((open_p == NULL) && (close_p == NULL)) return true;
    /* if only one is found, that's a mismatch */
    if ((open_p == NULL) && !(close_p == NULL)) {
        instr_err(
            BF_ERR_UNMATCHED_CLOSE,
            "Could not optimize due to unmatched ']'",
            ']',
            in_name
        );
        return false;
    }
    if ((close_p == NULL) && !(open_p == NULL)) {
        instr_err(
            BF_ERR_UNMATCHED_OPEN,
            "Could not optimize due to unmatched '['",
            '[',
            in_name
        );
        return false;
    }

    /* ensure that it opens before it closes */
    if (open_p > close_p) return false;
    /* if this point is reached, both are found. Ensure they are balanced. */
    return (find_loop_end(open_p, in_name) != NULL);
}

/* remove redundant instruction sequences like `<>` */
static nonnull_args bool remove_dead(SizedBuf *ir, const char *in_name) {
    /* code constructs that do nothing - either 2 adjacent instructions that
     * cancel each other out, or 256 consecutive `+` or `-` instructions that
     * loop the current cell back to its current value */
    char *str = ir->buf;
    const char *simple_patterns[] = {
        "<>",
        "><",
        "-+",
        "+-",
        ("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
         "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
         "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
         "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"),
        ("----------------------------------------------------------------"
         "----------------------------------------------------------------"
         "----------------------------------------------------------------"
         "----------------------------------------------------------------"),
    };
    /* don't want to compute these every loop, or even every run, so hard-code
     * the known sizes of the simple patterns here. */
    size_t simple_pattern_sizes[] = {2, 2, 2, 2, 256, 256};
    bool matched = false;
    char *match_start;
    const char *loop_end;
    do {
        matched = false;
        /* if str opens with a loop, that loop won't run, so remove it */
        if (*str == '[') {
            matched = true;
            loop_end = find_loop_end(str, in_name);
            if (loop_end == NULL) return false;
            memmove(str, loop_end, strlen(loop_end) + 1);
        }
        /* next, remove any matches for simple_patterns[*] */
        for (ufast_8 i = 0; i < 6 /* there are 6 patterns */; i++) {
            while (((match_start = strstr(str, simple_patterns[i])) != NULL)) {
                matched = true;
                memmove(
                    match_start,
                    match_start + simple_pattern_sizes[i],
                    strlen(match_start) - simple_pattern_sizes[i] + 1
                );
            }
        }
        /* finally, remove any loops that begin right after other loops end */
        while (((match_start = strstr(str, "][")) != NULL)) {
            matched = true;
            /* skip past the closing `]` */
            loop_end = find_loop_end(++match_start, in_name);
            if (loop_end == NULL) return false;
            memmove(match_start, loop_end, strlen(loop_end) + 1);
        }
    } while (matched);
    ir->sz = strlen(str) + 1;
    return true;
}

/* Merge `[-]` and `[+]` into `@` */
static nonnull_args void merge_set_zero(SizedBuf *ir) {
    char *p;
    /* handle [-] */
    while ((p = strstr(ir->buf, "[-]"))) {
        *p = '@';
        memmove(p + 1, p + 3, strlen(p + 3) + 1);
        ir->sz -= 2;
    }
    /* handle [+] */
    while ((p = strstr(ir->buf, "[+]"))) {
        *p = '@';
        memmove(p + 1, p + 3, strlen(p + 3) + 1);
        ir->sz -= 2;
    }
}

/* filter out all non-BF bytes, and anything that is trivially determined to be
 * dead code, or code with no effect (e.g. "+-" or "<>"), and replace "[-]" and
 * "[+]" with "@".
 *
 * "in_name" is the source filename, and is used only to generate error
 * messages. */
nonnull_args bool filter_dead(SizedBuf *bf_code, const char *in_name) {
    filter_non_bf(bf_code);
    if (bf_code->buf == NULL) return false;
    if (!loops_match(bf_code->buf, in_name)) return false;
    remove_dead(bf_code, in_name);
    if (bf_code->buf == NULL) return false;
    merge_set_zero(bf_code);
    return true;
}

#ifdef BFC_TEST
/* internal */
#include "unit_test.h"

static void strip_dead_code_test(void) {
    /* fill filterable with have a bunch of valid brainfuck code which does the
     * equivalent of ">+[->+<]", and ensure that it is optimized down to just
     * that sequence. */
    SizedBuf filterable = newbuf(574);
    append_obj(
        &filterable, "[+++++]><+---+++-[-][,[-][+>-<]]-+[-+]-+[]+-[]", 46
    );
    char *tgt = sb_reserve(&filterable, 256);
    memset(tgt, '+', 256);
    append_obj(&filterable, "[+-]>+", 5);
    tgt = sb_reserve(&filterable, 256);
    memset(tgt, '-', 256);
    append_obj(&filterable, "[->+<][,.]", 10);
    for (size_t i = 0; i < filterable.sz; ++i) {
        switch (((char *)filterable.buf)[i]) {
            case '<':
            case '>':
            case '+':
            case '-':
            case '[':
            case ']':
            case '.':
            case ',':
                continue;
            default:
                CU_FAIL_FATAL("filterable contains non-bf bytes");
                return;
        }
    }
    filter_dead(&filterable, "test");
    /* null-terminate before string comparison */
    tgt = sb_reserve(&filterable, 1);
    *tgt = '\0';
    CU_ASSERT_STRING_EQUAL(filterable.buf, ">+[->+<]");
    free(filterable.buf);
}

CU_pSuite register_optimize_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, strip_dead_code_test);
    return suite;
}

#endif
