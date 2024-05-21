/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A basic non-optimizing Brainfuck to x86_64 Linux ELF compiler.
 * */

/* C99 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
/* POSIX */
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
/* internal */
#include "eam_compile.h"

/* Return the permission mask to use for the output file */
mode_t _getperms() {
    /* The umask function sets the file mode creation mask to its argument and
     * returns the previous mask.
     *
     * The mask is used to set default file mode
     * the default permission for directories is (0777 & ~mask).
     * the default permission for normal files is (0666 & ~mask).
     *
     * There is no standard way to query the mask.
     * Because of this, the proper way to query the mask is to do the following.
     * Seriously. */
    mode_t mask = umask(0022); umask(mask);
    /* default to the default file permissions for group and other, but rwx for
     * the owner. */
    mode_t permissions = S_IRWXU | (~mask & 066);
    /* if the file's group can read it, it should also be allowed to execute it.
     * the same goes for other users. */
    if (permissions & S_IRGRP) {
        permissions += S_IXGRP;
    }
    if (permissions & S_IROTH) {
        permissions += S_IXOTH;
    }
    return permissions;
}

int main(int argc, char* argv[]) {
    int srcFD, dstFD;
    int result;
    /* default to false, set to true if -q was passed. */
    bool quiet = false;
    mode_t permissions = _getperms();
    if (argc < 3) {
        fputs("Not enough arguments provided.\n", stderr);
        return 2;
    }

    srcFD = open(argv[1], O_RDONLY);
    if (srcFD < 0) {
        fputs("Failed to open source file for reading.\n", stderr);
        return EXIT_FAILURE;
    }
    dstFD = open(argv[2], O_WRONLY+O_CREAT+O_TRUNC, permissions);
    if (dstFD < 0) {
        fputs("Failed to open destination file for writing.\n", stderr);
        return EXIT_FAILURE;
    }

    result = bfCompile(srcFD, dstFD);
    close(srcFD);
    close(dstFD);
    if ((!result) && (!quiet)) {
        fprintf(
            stderr,
            "Failed to compile character %c at line %d, column %d.\n"
            "Error message: \"%s\"\n",
            currentInstruction,
            currentInstructionLine,
            currentInstructionColumn,
            errorMessage
        );
        remove(argv[2]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
