/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Either provides a function that returns EAMBFC IR from an FD, or prints out a
 * step-by-step overview of the optimization process, depending on whether the
 * OPTIMIZE_STANDALONE macro is defined at compile time. */

/* C99 */
#include <stdio.h> /* sprintf, puts */
#include <stdlib.h> /* free, malloc */
#include <string.h> /* memmove, memset, strchr, strcpy, strlen, strstr */
/* internal */
#include "err.h" /* basic_err, instr_err */
#include "types.h" /* bool, uint, UINT64_MAX, uint*_t, INT64_MAX, sized_buf */
#include "util.h" /* append_obj, read_to_sized_buf */

/* filter out the non-bf characters from code->buf */
static void filter_non_bf(sized_buf *code) {
    sized_buf tmp = {0, 4096, malloc(4096)};
    char *str = code->buf;
    if (tmp.buf == NULL) {
        free(code->buf);
        code->buf = NULL;
        code->sz = 0;
    }
    char instr;
    for (size_t i = 0; i < code->sz; i++) {
        switch(instr = str[i]) {
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
    free(code->buf);
    code->sz = tmp.sz;
    code->capacity = tmp.capacity;
    code->buf = tmp.buf;
}

/* A function that skips past a matching ].
 * loop_start is a pointer to the [ at the start of the loop */
static const char *find_loop_end(const char *loop_start) {
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
        "UNMATCHED_OPEN",
        "Could not optimize due to unmatched '['",
        '['
    );
    return NULL;
}

/* return true if the loops are balanced, false otherwise */
static bool loops_match(const char *code) {
    const char *open_p = strchr(code, '[');
    const char *close_p = strchr(code, ']');
    /* if none are found, it's fine. */
    if ((open_p == NULL) && (close_p == NULL)) return true;
    /* if only one is found, that's a mismatch */
    if ((open_p == NULL) && !(close_p == NULL)) {
        instr_err(
            "UNMATCHED_CLOSE",
            "Could not optimize due to unmatched ']'",
            ']'
        );
        return false;
    }
    if ((close_p == NULL) && !(open_p == NULL)) {
        instr_err(
            "UNMATCHED_OPEN",
            "Could not optimize due to unmatched '['",
            '['
        );
        return false;
    }

    /* ensure that it opens before it closes */
    if (open_p > close_p) return false;
    /* if this point is reached, both are found. Ensure they are balanced. */
    return (find_loop_end(open_p) != NULL);
}

/* remove redundant instruction sequences like `<>` */
static void strip_dead(sized_buf *ir) {
    /* code constructs that do nothing - either 2 adjacent instructions that
     * cancel each other out, or 256 consecutive `+` or `-` instructions that
     * loop the current cell back to its current value */
    char *str = ir->buf;
    char *simple_patterns[] = {
        "<>", "><", "-+", "+-",
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++",
        "----------------------------------------------------------------"
        "----------------------------------------------------------------"
        "----------------------------------------------------------------"
        "----------------------------------------------------------------",
    };
    /* don't want to compute these every loop, or even every run, so hard-code
     * the known sizes sof the simple patterns here. */
    size_t simple_pattern_sizes[] = { 2, 2, 2, 2, 256, 256 };
    bool matched = false;
    char *match_start;
    const char *loop_end;
    do {
        matched = false;
        /* if str opens with a loop, that loop won't run - remove it */
        if (*str == '[') {
            matched = true;
            loop_end = find_loop_end(str);
            if (loop_end == NULL) {
                free(ir->buf);
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
            loop_end = find_loop_end(++match_start);
            if (loop_end == NULL) {
                free(ir->buf);
                ir->buf = NULL;
                return;
            }
            memmove(match_start, loop_end, strlen(loop_end) + 1);
        }
    } while (matched);
    ir->sz = strlen(str) + 1;
}

/* condense a sequence of identical instructions into an IR operation. */
static size_t condense(char instr, u64 consec_ct, char* dest) {
    char opcode;
#if SIZE_MAX < UINT64_MAX
    if (consec_ct > SIZE_MAX) {
        instr_err(
            "TOO_MANY_INSTRUCTIONS",
            "More than SIZE_MAX consecutive identical instructions. Somehow.",
            instr
        );
        return 0;
    }
#endif /* SIZE_MAX < UINT64_MAX */
    if (consec_ct == 1) {
        *dest = instr;
        return 1;
    } else switch(instr) {
      case '.':
      case ',':
      case ']':
      case '[':
        memset(dest, instr, consec_ct);
        return (size_t)consec_ct;
      case '>':
        if (consec_ct <= INT64_MAX) {
            opcode = '}';
        } else {
            instr_err(
                "TOO_MANY_INSTRUCTIONS",
                "Over 8192 Pebibytes of '>' in a row.",
                '>'
            );
            return 0;
        }
        break;
      case '<':
        if (consec_ct <= INT64_MAX) {
            opcode = '{';
        } else {
            instr_err(
                "TOO_MANY_INSTRUCTIONS",
                "Over 8192 Pebibytes of '<' in a row.",
                '<'
            );
            return 0;
        }
        break;
      /* for + and -, assume that consec_ct is less than 256, as a larger
       * value would have been optimized down. */
      case '+':
        opcode = '#';
        break;
      case '-':
        opcode = '=';
        break;
      default:
        return 0;
        break;
      }
    return (size_t)sprintf(dest, "%c%" PRIx64, opcode, consec_ct);
}

/* Substitute instructions
 *
 * SUBSTITUTIONS:
 *
 * N consecutive `>` instructions are replaced with `}N`.
 * N consecutive `<` instructions are replaced with `{N`.
 * N consecutive `+` instructions are replaced with `#N`.
 * N consecutive `-` instructions are replaced with `=N`.
 *
 * single `+`, `-`, `<`, and `>` instructions are left as is.
 *
 * `[+]` and `[-]` both get replaced with `@`.
 *
 * all `,` and `.` instructions are left unchanged, as are any `[` or `]`
 * instructions not part of the two sequences that are replaced with `@`. */
static void instr_merge(sized_buf *ir) {
    char *str = ir->buf;
    /* used to check what's between [ and ] if they're 2 apart */
    char current_mode;
    char prev_mode = *str;
    char *new_str = malloc(ir->sz);
    if (new_str == NULL) {
        alloc_err();
        return;
    }
    char *p = new_str;
    u64 consec_ct = 1;
    size_t i;
    size_t skip;
    /* condese consecutive identical instructions */
    for (i = 1; i < ir->sz; i++) {
        current_mode = *(str+i);
        if (current_mode != prev_mode) {
            if (!((skip = condense(prev_mode, consec_ct, p)))) {
                free(new_str);
                instr_err(
                    "INTERNAL_ERROR",
                    "Failed to condense consecutive instructions",
                    prev_mode
                );
                return;
            }
            p += skip;
            consec_ct = 1;
            prev_mode = current_mode;
        } else {
            if (consec_ct == UINT64_MAX) {
                instr_err(
                    "TOO_MANY_INSTRUCTIONS",
                    "More than 16384 PiB of identical instructions in a row.",
                    prev_mode
                );
                return;
            } else {
                consec_ct ++;
            }
        }
    }
    *p = '\0'; /* NULL terminate the new string */

    /* handle [-] */
    while ((p = strstr(new_str, "[-]"))) {
        *p = '@';
        memmove(p + 1, p + 3, strlen(p + 3) + 1);
    }
    /* handle [+] */
    while ((p = strstr(new_str, "[+]"))) {
        *p = '@';
        memmove(p + 1, p + 3, strlen(p + 3) + 1);
    }
    strcpy(str, new_str);
    ir->sz = strlen(str);
    free(new_str);
}

#ifdef OPTIMIZE_STANDALONE
/* POSIX */
#include <fcntl.h> /* open */
#include <unistd.h> /* close */
/* used for testing purposes
 * inspired by Python's `if __name__ == "__main__" idiom
 * compile with flag -D OPTIMIZE_STANDALONE to enable this and compile a
 * standalone program that provides insight into the optimization process. */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fputs("Not enough arguments.\n", stderr);
        return EXIT_FAILURE;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        fputs("Failed to open file.\n", stderr);
        return EXIT_FAILURE;
    }

    sized_buf ir = {0, 4096, malloc(4096)};
    if (ir.buf == NULL) {
        alloc_err();
        close(fd);
        return 1;
    }
    read_to_sized_buf(&ir, fd);

    puts("Stage 1:");
    filter_non_bf(&ir);
    if (ir.buf == NULL) {
        fputs("Stage 1 came back null.\n", stderr);
        return EXIT_FAILURE;
    }
    puts(ir.buf);

    puts("Stage 2:");
    if (!loops_match(ir.buf)) {
        free(ir.buf);
        fputs("Mismatched [ and ]; refusing to continue.\n", stderr);
        return EXIT_FAILURE;
    }

    strip_dead(&ir);
    if (ir.buf == NULL) {
        fputs("Stage 2 came back null.\n", stderr);
        return EXIT_FAILURE;
    }
    puts(ir.buf);

    puts("Stage 3:");
    instr_merge(&ir);
    if (ir.buf == NULL) {
        fputs("Stage 3 came back null.\n", stderr);
        return EXIT_FAILURE;
    }
    puts(ir.buf);

    free(ir.buf);
}
#else /* OPTIMIZE_STANDALONE */
/* Reads the content of the file fd, and returns a string containing optimized
 * internal intermediate representation of that file's code.
 * fd must be open for reading already, no check is performed.
 * Calling function is responsible for `free`ing the returned string. */
void to_ir(sized_buf *src) {
    filter_non_bf(src);
    if (src->buf == NULL) {
        free(src->buf);
        src->buf = NULL;
        return;
    }
    if (!loops_match(src->buf)) {
        free(src->buf);
        src->buf = NULL;
        return;
    }
    strip_dead(src);
    if (src->buf == NULL) {
        free(src->buf);
        src->buf = NULL;
        return;
    }
    instr_merge(src);
    src->sz = strlen(src->buf) + 1;
}
#endif /* OPTIMIZE_STANDALONE */
