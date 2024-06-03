/* C99 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
/* #include <unistd.h> */
/* internal */
/* #include "config.h" */
#include "eambfc_types.h"

#define MALLOC_CHUNK_SIZE 0x100

/* return a pointer to a string containing just the brainfuck characters.
 *
 * Calling function is responsible for `free`ing the value. */
char *filterNonBFChars(int in_fd) {
    size_t optim_sz = 0;
    size_t limit = MALLOC_CHUNK_SIZE;
    char *filtered = (char *)malloc(limit);
    char *p = filtered;
    if (filtered == NULL) return NULL;
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
            *(p++) = instr;
            optim_sz++;
            if (optim_sz == limit) {
                limit += MALLOC_CHUNK_SIZE;
                filtered = realloc(filtered, limit);
                if (filtered == NULL) return NULL;
                p = filtered + optim_sz;
            }
            break;
        }
    }
    /* null terminate it */
    *p = '\0';
    optim_sz++;
    /* reallocate it to the exact size needed */
    filtered = realloc(filtered, optim_sz);
    return filtered;
}

/* A function that skips past a matching ].
 * search_start is a pointer to the character after the [ */
char *findLoopEnd(char *search_start) {
    char *open_p = strchr(search_start, '[');
    /* If no match is found for open_p, set it to the end of the string*/
    char *close_p = strchr(search_start, ']');

    if (close_p == NULL) return NULL;
    /* indicates a nested loop begins before the end of the current loop. */
    if ((open_p != NULL) && (close_p > open_p)) {
        close_p = findLoopEnd(open_p + 1);
    }
    return close_p + 1;
}

#define SIMPLE_PATTERN_NUM 6
/* deal with no more than this many matches of a given pattern per pass */
#define MATCH_COUNT 16
/* expand to the common form of all regexec calls in stripUselessCode */
#define regexMatch(p, i) regexec(&p, filtered_str, MATCH_COUNT, matches[i], 0)
/* remove redundant instructions like `<>` */
char *stripUselessCode(char *str) {
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
            match_start++;
            loop_end = findLoopEnd(match_start + 1);
            memmove(match_start, loop_end, strlen(loop_end) + 1);
        }
    } while (matched);

    str = (char *)realloc(str, strlen(str) + 1);
    return str;
}

char *mergeInstructions(char *s) {
    /* TODO */
    return s;
}

/* used for testing purposes; Python's `if __name__ == "__main__" idiom */
#ifdef SIMPLIFY_STANDALONE
int main(int argc, char *argv[]) {
    if (argc < 2) {
        puts("Not enough arguments.");
        return EXIT_FAILURE;
    }
    puts("stage 1:");
    char *s = filterNonBFChars(open(argv[1], O_RDONLY));
    puts(s);
    puts("stage 2:");
    s = stripUselessCode(s);
    puts(s);
    puts("stage 3:");
    s = mergeInstructions(s);
    puts(s);
    free(s);
}
#endif /* SIMPLIFY_STANDALONE */
