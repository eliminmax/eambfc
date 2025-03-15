/*SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: 0BSD
 *
 * Uses argv[1], and prints subsequent args that match the glob. */

/* C99 */
#include <stdio.h>
/* POSIX */
#include <fnmatch.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Not enough arguments!\n");
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        switch (fnmatch(argv[1], argv[i], 0)) {
        case 0: puts(argv[i]); break;
        case FNM_NOMATCH: break;
        default: return 1;
        }
    }
    return 0;
}
