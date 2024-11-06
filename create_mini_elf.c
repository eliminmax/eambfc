/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A simple program that creates a minimal AMD x86_64 Linux ELF binary that
 * simply calls exit(0), to validate that a system can run such binaries. */

#include <stdio.h> /* fputs, stderr, tmpfile, fileno, fclose */
#include <stdlib.h> /* EXIT_FAILURE, EXIT_SUCCESS */
/* POSIX */
#include <fcntl.h> /* open, O_* */
#include <unistd.h> /* close */
/* internal */
#include "arch_inter.h" /* X86_64_INTER */
#include "compile.h" /* bf_compile */

int main(void) {
    /* a tmpfile will be open by default, so it's a portable way to have an
     * empty source file. */
    FILE* tmp_file = tmpfile();
    if (tmp_file == NULL) {
        fputs("Failed to open tmpfile.\n", stderr);
        return EXIT_FAILURE;
    }

    /* bf_compile expects a fd number rather than a file pointer */
    int in_fd = fileno(tmp_file);
    if (in_fd == -1) {
        fputs("Failed to get file descriptor for tmp_file.\n", stderr);
        fclose(tmp_file);
        return EXIT_FAILURE;
    }

    /* compile an empty program called mini_elf */
    int out_fd = open("mini_elf", O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (out_fd == -1) {
        fclose(tmp_file);
        fputs("Failed to open mini_elf for writing.\n", stderr);
        return EXIT_FAILURE;
    }
    int ret = bf_compile(&X86_64_INTER, in_fd, out_fd, false, 1) ?
        EXIT_SUCCESS : EXIT_FAILURE;
    fclose(tmp_file);
    close(out_fd);
    return ret;
}
