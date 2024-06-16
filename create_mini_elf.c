/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A simple program that reuses functions and macros from eam_compile.c and
 * eam_compiler_macros.h to create a minimal AMD x86_64 Linux ELF binary that
 * simply calls exit(0), to validate that a system can run Linux binaries. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
/* POSIX */
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
/* internal */
#include "eam_compile.h"

/* not nearly as robust as the actual code */
int main(void) {
    /* Don't bother worrying about umask for this one. */
    int in_fd = open("/dev/null", O_RDONLY);
    if (in_fd == -1) {
        fputs("Failed to open /dev/null for reading.\n", stderr);
        return EXIT_FAILURE;
    }
    int out_fd = open("mini_elf", O_WRONLY+O_CREAT+O_TRUNC, 0755);
    if (out_fd == -1) {
        close(in_fd);
        fputs("Failed to open mini_elf for writing.\n", stderr);
        return EXIT_FAILURE;
    }
    return bfCompile(in_fd, out_fd, false) ? EXIT_SUCCESS : EXIT_FAILURE;
}
