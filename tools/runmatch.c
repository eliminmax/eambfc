/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: 0BSD
 *
 * Internal tool for eambfc testing
 *
 * USAGSE:
 * pass a fnmatch-compatible glob pattern as argv[1].
 *
 * pass the command and flags to run, followed by the files to run the glob
 * against, the remaining args, separated by "{-}", as it's not meaningful to
 * any of the commands this tool is needed for */

/* stdlib */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <fnmatch.h>
#include <unistd.h>

#define MAX_ARGS 64

int main(int argc, char *argv[]) {
    char *chld_args[MAX_ARGS + 1] = {0};
    if (argc < 2) {
        fputs("Not enough arguments\n", stderr);
        return EXIT_FAILURE;
    }

    int chld_argc = 0;
    char matched = 0;
    char split = 0;
    char *pat = argv[1];
    int argi;

    for (argi = 2; argi < argc; argi++) {
        if (strcmp(argv[argi], "{-}") == 0) {
            split = 1;
            break;
        }
        if (chld_argc == MAX_ARGS) {
            fputs("Too many arguments to pass to child\n", stderr);
            return EXIT_FAILURE;
        }
        chld_args[chld_argc++] = argv[argi];
    }

    if (!split) {
        fputs("Missing delimiter between command and args\n", stderr);
        return EXIT_FAILURE;
    }

    for (argi++; argi < argc; argi++) {
        switch (fnmatch(pat, argv[argi], 0)) {
        case 0:
            if (chld_argc == MAX_ARGS) {
                fputs("Too many arguments to pass to child\n", stderr);
                return EXIT_FAILURE;
            }
            matched = 1;
            chld_args[chld_argc++] = argv[argi];
            break;
        case FNM_NOMATCH: break;
        default: return EXIT_FAILURE;
        }
    }

    if (!matched) return EXIT_SUCCESS;

    execvp(chld_args[0], chld_args);
    fputs("Failed to exec child\n", stderr);
    return EXIT_SUCCESS;
}
