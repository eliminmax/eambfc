/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A basic non-optimizing Brainfuck to x86_64 Linux ELF compiler.
 * */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "eam_compile.h"

int main(int argc, char* argv[]) {
    int srcFD, dstFD, result;

    if (argc < 3) {
        fputs("Not enough arguments provided.\n", stderr);
        return 2;
    }
    srcFD = open(argv[1], O_RDONLY);
    if (srcFD < 0) {
        fputs("Failed to open source file for reading.\n", stderr);
        return 2;
    }
    dstFD = open(argv[2], O_WRONLY+O_CREAT+O_TRUNC, S_IRWXU);
    if (dstFD < 0) {
        fputs("Failed to open destination file for writing.\n", stderr);
        return 2;
    }
    result = bfCompile(srcFD, dstFD);
    close(srcFD);
    close(dstFD);
    if (!result) {
        fprintf(
                stderr,
                "Failed to compile character %c at line %d, column %d.\n",
                currentInstruction,
                currentInstructionLine,
                currentInstructionColumn
               );
        fprintf(
                stderr,
                "Error message: \"%s\"\n",
                errorMessage
               );
        return 1;
    }
    return 0;
}
