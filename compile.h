/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file provides an interface to compile.c. */

#ifndef BFC_COMPILE_H
#define BFC_COMPILE_H 1
/* internal */
#include "arch_inter.h"
#include "types.h"

/* Compile code in source file to destination file.
 * Parameters:
 * - inter is a pointer to the arch_inter backend used to provide the functions
 *   that compile brainfuck and EAMBFC-IR into machine code.
 * - in_fd is a brainfuck source file, open for reading.
 * - out_fd is the destination file, open for writing.
 * - optimize is a boolean indicating whether to optimize code before compiling.
 * - tape_blocks is the number of 4-KiB blocks to allocate for the tape.
 *
 * Returns true if compilation was successful, and false if any issues occurred.
 *
 * It does not verify that in_fd and out_fd are valid file descriptors,
 * nor that they are open properly.
 *
 * If it runs into any problems, it prints an appropriate error message.
 * It will try to continue after hitting certain errors, so that the resulting
 * binary can still be examined and debugged. If that is not needed, the output
 * file can be deleted, as it is in main.c if bf_compile returns false.
 *
 * If optimize is set to true, it first converts the contents of in_fd to a
 * simple internal representation (EAMBFC-IR, which is a superset of brainfuck),
 * then compiles that, typically cutting the size of the output code by a decent
 * amount. */
bool bf_compile(
    const arch_inter *inter,
    const char *in_name,
    const char *out_name,
    int in_fd,
    int out_fd,
    bool optimize,
    u64 tape_blocks
);

#endif /* BFC_COMPILE_H */
