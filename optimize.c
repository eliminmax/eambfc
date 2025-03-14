/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Either provides a function that returns EAMBFC IR from an FD, or prints out a
 * step-by-step overview of the optimization process, depending on whether the
 * OPTIMIZE_STANDALONE macro is defined at compile time. */

/* C99 */
#include <string.h>
/* internal */
#include "attributes.h"
#include "err.h"
#include "resource_mgr.h"
#include "types.h"
#include "util.h"

/* filter out the non-bf characters from code->buf */
static nonnull_args void filter_non_bf(sized_buf *code) {
    sized_buf tmp = {.sz = 0, .capacity = 4096, .buf = mgr_malloc(4096)};
    char instr;
    for (size_t i = 0; i < code->sz; i++) {
        switch (instr = ((char *)(code->buf))[i]) {
        case '[':
        case '-':
        case '.':
        case '<':
        case '>':
        case ',':
        case '+':
        case ']': append_obj(&tmp, &instr, 1); break;
        }
    }
    instr = '\0';
    /* null terminate it */
    append_obj(&tmp, &instr, 1);
    code->sz = tmp.sz;
    code->capacity = tmp.capacity;
    code->buf = tmp.buf;
}

static void instr_err(
    bf_err_id id, const char *msg, char instr, const char *in_name
) {
    bf_comp_err e = {
        .id = id,
        .msg = msg,
        .instr = instr,
        .file = in_name,
        .has_instr = true,
        .has_location = false,
    };
    display_err(e);
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

#define REPSTR16(s) s s s s s s s s s s s s s s s s
#define REPSTR64(s) REPSTR16(s) REPSTR16(s) REPSTR16(s) REPSTR16(s)
#define REPSTR256(s) REPSTR64(s) REPSTR64(s) REPSTR64(s) REPSTR64(s)

/* remove redundant instruction sequences like `<>` */
static nonnull_args void remove_dead(sized_buf *ir, const char *in_name) {
    /* code constructs that do nothing - either 2 adjacent instructions that
     * cancel each other out, or 256 consecutive `+` or `-` instructions that
     * loop the current cell back to its current value */
    char *str = ir->buf;
    const char *simple_patterns[] = {
        "<>",
        "><",
        "-+",
        "+-",
        REPSTR256("+"),
        REPSTR256("-"),
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
            if (loop_end == NULL) {
                mgr_free(ir->buf);
                ir->buf = NULL;
                return;
            }
            memmove(str, loop_end, strlen(loop_end) + 1);
        }
        /* next, remove any matches for simple_patterns[*] */
        for (u8 i = 0; i < 6 /* there are 6 patterns */; i++) {
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
            if (loop_end == NULL) {
                mgr_free(ir->buf);
                ir->buf = NULL;
                return;
            }
            memmove(match_start, loop_end, strlen(loop_end) + 1);
        }
    } while (matched);
    ir->sz = strlen(str) + 1;
}

/* Merge `[-]` and `[+]` into `@` */
static nonnull_args void merge_set_zero(sized_buf *ir) {
    char *str = ir->buf;
    char *p;

    /* handle [-] */
    while ((p = strstr(str, "[-]"))) {
        *p = '@';
        memmove(p + 1, p + 3, strlen(p + 3) + 1);
    }
    /* handle [+] */
    while ((p = strstr(str, "[+]"))) {
        *p = '@';
        memmove(p + 1, p + 3, strlen(p + 3) + 1);
    }
    ir->sz = strlen(str);
}

/* Reads the content of the file fd, and returns a string containing optimized
 * internal intermediate representation of that file's code.
 * fd must be open for reading already, no check is performed.
 * Calling function is responsible for `mgr_free`ing the returned string. */
nonnull_args bool filter_dead(sized_buf *src, const char *in_name) {
    filter_non_bf(src);
    if (src->buf == NULL) {
        mgr_free(src->buf);
        src->buf = NULL;
        return false;
    }
    if (!loops_match(src->buf, in_name)) {
        mgr_free(src->buf);
        src->buf = NULL;
        return false;
    }
    remove_dead(src, in_name);
    if (src->buf == NULL) {
        mgr_free(src->buf);
        src->buf = NULL;
        return false;
    }
    merge_set_zero(src);
    return true;
}
