/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Either provides a function that returns EAMBFC IR from an FD, or prints out a
 * step-by-step overview of the optimization process, depending on whether the
 * OPTIMIZE_STANDALONE macro is defined at compile time. */

/* C99 */
#include <stdio.h> /* sprintf, puts */
#include <stdlib.h> /* free malloc realloc */
#include <string.h> /* memmove memset strchr strcpy strlen strstr */
/* POSIX */
#include <unistd.h> /* read */
/* internal */
#include "err.h" /* basic_err, instr_err */
#include "types.h" /* bool, uint, UINT64_MAX, uint*_t, INT64_MAX */
#define MALLOC_CHUNK_SIZE 0x100

static size_t optim_sz;
/* return a pointer to a string containing just the brainfuck characters. */
static char *filter_non_bf(int in_fd) {
    optim_sz = 0;
    size_t limit = MALLOC_CHUNK_SIZE;
    char *filtered = malloc(limit);
    if (filtered == NULL) {
        basic_err("FAILED_MALLOC", "Failed to allocate buffer");
        return NULL;
    }
    char instr;
    while (read(in_fd, &instr, 1)) {
        switch(instr) {
          case '[':
          case '-':
          case '.':
          case '<':
          case '>':
          case ',':
          case '+':
          case ']':
            *(filtered + optim_sz++) = instr;
            if (optim_sz == limit) {
                limit += MALLOC_CHUNK_SIZE;
                filtered = realloc(filtered, limit);
                if (filtered == NULL) {
                    basic_err("FAILED_MALLOC", "Failed to extend buffer");
                    return NULL;
                }
            }
            break;
        }
    }
    /* null terminate it */
    *(filtered + optim_sz++) = '\0';
    return filtered;
}

/* A function that skips past a matching ].
 * loop_start is a pointer to the [ at the start of the loop */
static char *find_loop_end(char *loop_start) {
    uint nest_level = 1;
    char *p = loop_start;
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

/* recursively ensure that the loops are balanced */
static bool loops_match(char *code) {
    char *open_p = strchr(code, '[');
    char *close_p = strchr(code, ']');
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

#define SIMPLE_PATTERN_NUM 6
/* remove redundant instructions like `<>` */
static char *strip_dead(char *str) {
    /* matches[0] is for nop_pat matches. matches[1] is for dead_loop_pat */
    /* code constructs that do nothing - either 2 adjacent instructions that
     * cancel each other out, or 256 consecutive `+` or `-` instructions that
     * loop the current cell back to its current value */
    char *simple_patterns[] = {
        "<>", "><", "-+", "+-",
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"\
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"\
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"\
        "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++",
        "----------------------------------------------------------------"\
        "----------------------------------------------------------------"\
        "----------------------------------------------------------------"\
        "----------------------------------------------------------------"
    };
    /* don't want to compute these every loop, or even every run, so hard-code
     * the known sizes sof the simple patterns here. */
    size_t simple_pattern_sizes[] = { 2, 2, 2, 2, 256, 256 };
    bool matched = false;
    char *match_start;
    char *loop_end;
    do {
        matched = false;
        /* if str opens with a loop, that loop won't run - remove it */
        if (*str == '[') {
            matched = true;
            loop_end = find_loop_end(str);
            if (loop_end == NULL) {
                free(str);
                return NULL;
            }
            memmove(str, loop_end, strlen(loop_end) + 1);
        }
        /* next, remove any matches for simple_patterns[*] */
        for (uint8_t i = 0; i < SIMPLE_PATTERN_NUM; i++) {
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
                free(str);
                return NULL;
            }
            memmove(match_start, loop_end, strlen(loop_end) + 1);
        }
    } while (matched);
    optim_sz = strlen(str) + 1;
    return str;
}


/* Replace certain instruction sequences with alternative instructions that
 * can be compiled to fewer machine instructions with the same effect.
 *
 * Should be done on code that was already optimized.
 *
 * SUBSTITUTIONS:
 *
 * >*N: }N
 * <*N: {N
 * +*N: #N   | chosen as a "double stroked" version of the
 * -*N: =N   | symbol, not for mathematical meaning.
 *
 * single +, -, <, and > instructions are left as is.
 *
 * [+] or [-] | @ | both of these set the cell to 0. */

/* condense a sequence of identical instructions into IR operation. */
static size_t condense(char instr, uint64_t consec_ct, char* dest) {
    char opcode;
#if SIZE_MAX < UINT64_MAX
    if (consec_t > SIZE_MAX) {
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

static char *instr_merge(char *s) {
    /* used to check what's between [ and ] if they're 2 apart */
    char current_mode;
    char prev_mode = *s;
    char *new_str = malloc(optim_sz);
    if (new_str == NULL) {
        return NULL;
    }
    char *p = new_str;
    uint64_t consec_ct = 1;
    size_t i;
    size_t skip;
    /* condese consecutive identical instructions */
    for (i = 1; i < optim_sz; i++) {
        current_mode = *(s+i);
        if (current_mode != prev_mode) {
            if (!((skip = condense(prev_mode, consec_ct, p)))) {
                free(new_str);
                instr_err(
                    "INTERNAL_ERROR",
                    "Failed to condense consecutive instructions",
                    prev_mode
                );
                return NULL;
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
                return NULL;
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
    strcpy(s, new_str);
    free(new_str);
    return s;
}

#ifdef OPTIMIZE_STANDALONE
#include <fcntl.h> /* open */
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
    puts("Stage 1:");
    char *s = filter_non_bf(fd);
    if (s == NULL) {
        fputs("Stage 1 came back null.\n", stderr);
        return EXIT_FAILURE;
    }
    puts(s);
    if (!loops_match(s)) {
        free(s);
        fputs("Mismatched [ and ]; refusing to continue.\n", stderr);
        return EXIT_FAILURE;
    }
    puts("Stage 2:");
    s = strip_dead(s);
    if (s == NULL) {
        fputs("Stage 2 came back null.\n", stderr);
        return EXIT_FAILURE;
    }
    puts(s);
    puts("Stage 3:");
    s = instr_merge(s);
    if (s == NULL) {
        fputs("Stage 3 came back null.\n", stderr);
        return EXIT_FAILURE;
    }
    puts(s);
    free(s);
}
#else /* OPTIMIZE_STANDALONE */
/* Reads the content of the file fd, and returns a string containing optimized
 * internal intermediate representation of that file's code.
 * fd must be open for reading already, no check is performed.
 * Calling function is responsible for `free`ing the returned string. */
char *to_ir(int fd) {
    char *bf_code = filter_non_bf(fd);
    if (bf_code == NULL) return NULL;
    if (!loops_match(bf_code)) {
        free(bf_code);
        return NULL;
    }
    bf_code = strip_dead(bf_code);
    if (bf_code == NULL) return NULL;
    return instr_merge(bf_code);
}
#endif /* OPTIMIZE_STANDALONE */
