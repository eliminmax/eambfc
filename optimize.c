/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Either provides a function that returns EAMBFC IR from an FD, or prints out a
 * step-by-step overview of the optimization process, depending on whether the
 * OPTIMIZE_STANDALONE macro is defined at compile time. */

/* C99 */
#include <inttypes.h> /* provides a superset of stdint with macros for printf */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
/* internal */
#include "err.h"

#define MALLOC_CHUNK_SIZE 0x100

static size_t optim_sz;
/* return a pointer to a string containing just the brainfuck characters. */
static char *filterNonBFChars(int in_fd) {
    optim_sz = 0;
    size_t limit = MALLOC_CHUNK_SIZE;
    char *filtered = malloc(limit);
    if (filtered == NULL) {
        appendError('?', "Failed to allocate buffer", "ICE_ICE_BABY");
        return NULL;
    }
    char instr;
    while (read(in_fd, &instr, sizeof(char))) {
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
                    appendError('?', "Failed to extend buffer", "ICE_ICE_BABY");
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
 * search_start is a pointer to the character after the [ */
static char *findLoopEnd(char *search_start) {
    char *open_p = strchr(search_start, '[');
    /* If no match is found for open_p, set it to the end of the string */
    char *close_p = strchr(search_start, ']');

    if (close_p == NULL) {
        appendError(
            '[',
            "Could not optimize due to unmatched '['",
            "UNMATCHED_OPEN"
        );
        return NULL;
    }
    /* indicates a nested loop begins before the end of the current loop. */
    if ((open_p != NULL) && (close_p > open_p)) {
        close_p = findLoopEnd(open_p + 1);
    }
    return close_p + 1;
}

/* recursively ensure that the loops are balanced */
static bool loopsMatch(char *code) {
    char *open_p = strchr(code, '[');
    char *close_p = strchr(code, ']');
    /* if none are found, it's fine. */
    if ((open_p == NULL) && (close_p == NULL)) return true;
    /* if only one is found, that's a mismatch */
    if ((open_p == NULL) && !(close_p == NULL)) {
        appendError(
            '?',
            "Could not optimize due to unmatched ']'",
            "UNMATCHED_CLOSE"
        );
        return false;
    }
    if ((close_p == NULL) && !(open_p == NULL)) {
        appendError(
            '?',
            "Could not optimize due to unmatched '['",
            "UNMATCHED_OPEN"
        );
        return false;
    }

    /* ensure that it opens before it closes */
    if (open_p > close_p) return false;
    /* if this point is reached, both are found. Ensure they are balanced. */
    return (findLoopEnd(open_p + 1) != NULL);
}

#define SIMPLE_PATTERN_NUM 6
/* remove redundant instructions like `<>` */
static char *stripUselessCode(char *str) {
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
    /* don't want to compute these every loop */
    size_t simple_pattern_sizes[] = { 2, 2, 2, 2, 256, 256 };
    bool matched = false;
    char *match_start;
    char *loop_end;
    do {
        matched = false;
        /* if str opens with a loop, that loop won't run - remove it */
        if (*str == '[') {
            matched = true;
            loop_end = findLoopEnd(str + 1);
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
            loop_end = findLoopEnd(++match_start + 1);
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
 * can be compiled to fewer machine instructions with the effect.
 *
 * Should be done on code that was already optimized.
 *
 * SUBSTITUTIONS:
 *
 * >*N | (1 < N <= INT8_MAX): }N
 * <*N | (1 < N <= INT8_MAX): {N
 * >*N | (INT8_MAX < N <= INT16_MAX): )N
 * <*N | (INT8_MAX < N <= INT16_MAX): (N
 * >*N | (INT16_MAX < N <= INT32_MAX): $N | symbols inspired by regex here
 * <*N | (INT16_MAX < N <= INT32_MAX): ^N | though I doubt it'll ever compile
 *                                        | 32KiB of consecutive < or >
 * >*N | (INT32_MAX < N <= INT64_MAX): nN | (the first n or N is the opcode)
 * <*N | (INT32_MAX < N <= INT64_MAX): NN | inspired by Vim keybindings
 *
 * +*N : #N   | chosen as a "double stroked" version of the
 * -*N : =N   | symbol, not for mathematical meaning.
 *
 * single +, -, <, and > instructions are left as is.
 *
 * [+] or [-] | @ | both of these set the cell to 0. */

/* condense a sequence of identical instructions into IR operation. */
static size_t condense(char instr, uint64_t consec_ct, char* dest) {
    char opcode;
#if SIZE_MAX < UINT64_MAX
    if (consec_t > SIZE_MAX) {
        appendError(
            instr,
            "More than SIZE_MAX consecutive identical instructions. Somehow.",
            "THIS_SHOULD_BE_IMPOSSIBLE"
        );
        return 0;
    }
#endif
    if (consec_ct == 1) {
        *dest = instr;
        return 1;
    } else switch(instr) {
      case '.':
      case ',':
      case ']':
      case '[':
        memset(dest, instr, sizeof(char) * consec_ct);
        return (size_t)consec_ct;
      case '>':
        if      (consec_ct <= INT8_MAX)  opcode = '}';
        else if (consec_ct <= INT16_MAX) opcode = ')';
        else if (consec_ct <= INT32_MAX) opcode = '$';
        else if (consec_ct <= INT64_MAX) opcode = 'n';
        else {
            appendError(
                '>',
                "Over 8192 Pebibytes of '>' in a row.",
                "PUTTING_WHAT_THE_ACTUAL_FUCK_IN_BRAINFUCK"
            );
            return 0;
        }
        break;
      case '<':
        if      (consec_ct <= INT8_MAX)  opcode = '{';
        else if (consec_ct <= INT16_MAX) opcode = '(';
        else if (consec_ct <= INT32_MAX) opcode = '^';
        else if (consec_ct <= INT64_MAX) opcode = 'N';
        else {
            appendError(
                '<',
                "Over 8192 Pebibytes of '<' in a row.",
                "PUTTING_WHAT_THE_ACTUAL_FUCK_IN_BRAINFUCK"
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

static char *mergeInstructions(char *s) {
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
                appendError(
                    prev_mode,
                    "Failed to consense consecutive instructions",
                    "ICE_ICE_BABY"
                );
                return NULL;
            }
            p += skip;
            consec_ct = 1;
            prev_mode = current_mode;
        } else {
            consec_ct ++;
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

/* used for testing purposes
 * inspired by Python's `if __name__ == "__main__" idiom
 * compile with flag -D OPTIMIZE_STANDALONE to enable this and compile a
 * standalone program that provides insight into the optimization process. */
#ifdef OPTIMIZE_STANDALONE
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fputs("Not enough arguments.\n", stderr);
        return EXIT_FAILURE;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        fputs("failed to open file.\n", stderr);
        return EXIT_FAILURE;
    }
    puts("stage 1:");
    char *s = filterNonBFChars(fd);
    if (s == NULL) {
        fputs("stage 1 came back null.\n", stderr);
        return EXIT_FAILURE;
    }
    puts(s);
    if (!loopsMatch(s)) {
        free(s);
        fputs("Mismatched [ and ]. refusing to continue.\n", stderr);
        return EXIT_FAILURE;
    }
    puts("stage 2:");
    s = stripUselessCode(s);
    if (s == NULL) {
        fputs("stage 2 came back null.\n", stderr);
        return EXIT_FAILURE;
    }
    puts(s);
    puts("stage 3:");
    s = mergeInstructions(s);
    if (s == NULL) {
        fputs("stage 3 came back null.\n", stderr);
        return EXIT_FAILURE;
    }
    puts(s);
    free(s);
}
#else
/* Reads the content of the file fd, and returns a string containing optimized
 * internal intermediate representation of that file's code.
 * fd must be open for reading already, no check is performed.
 * Calling function is responsible for `free`ing the returned string. */
char *toIR(int fd) {
    char *bf_code = filterNonBFChars(fd);
    if (bf_code == NULL) return NULL;
    if (!loopsMatch(bf_code)) {
        free(bf_code);
        return NULL;
    }
    bf_code = stripUselessCode(bf_code);
    if (bf_code == NULL) return NULL;
    return mergeInstructions(bf_code);
}
#endif /* OPTIMIZE_STANDALONE */
