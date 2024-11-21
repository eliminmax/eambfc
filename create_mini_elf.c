/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A simple program that creates a minimal AMD x86_64 Linux ELF binary that
 * simply calls exit(0), to validate that a system can run such binaries. */

#include <stdio.h> /* fputs, stderr, tmpfile, fileno, fclose */
#include <stdlib.h> /* EXIT_FAILURE, EXIT_SUCCESS, atexit */
/* POSIX */
#include <fcntl.h> /* O_* */
/* internal */
#include "arch_inter.h" /* X86_64_INTER */
#include "compile.h" /* bf_compile */
#include "resource_mgr.h" /* register_mgr, mgr_open_m, mgr_close */

static FILE *tmp_file;

static void close_tmp_file(void) {
    if (tmp_file != NULL) fclose(tmp_file);
    tmp_file = NULL;
}

int main(void) {
    /* register atexit function to clean up any open files or memory allocations
     * left behind. */
    register_mgr();

    /* a tmpfile will be empty and open for reading by default, so it's a
     * portable way to have an empty source file for testing. */
    if ((tmp_file = tmpfile()) == NULL) {
        fputs("Failed to open tmpfile.\n", stderr);
        exit(EXIT_FAILURE);
    }

    /* register the function to close tmp_file with atexit, so that if
     * bf_compile runs into an allocation failure, it's not left open when
     * program exits. */
    /* atexit returns zero on success and a non-zero value on failure. */
    if (atexit(close_tmp_file)) {
        fputs("Could not register close_tmp_file with atexit.\n", stderr);
    }

    /* bf_compile expects a fd number rather than a file pointer */
    int in_fd = fileno(tmp_file);
    if (in_fd == -1) {
        fputs("Failed to get file descriptor for tmp_file.\n", stderr);
        exit(EXIT_FAILURE);
    }

    /* compile an empty program called mini_elf */
    int out_fd = mgr_open_m("mini_elf", O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (out_fd == -1) {
        fputs("Failed to open mini_elf for writing.\n", stderr);
        exit(EXIT_FAILURE);
    }
    int ret = bf_compile(&X86_64_INTER, in_fd, out_fd, false, 1) ?
                  EXIT_SUCCESS :
                  EXIT_FAILURE;
    mgr_close(out_fd);
    close_tmp_file();
    return ret;
}
