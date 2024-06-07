/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file provides an interface to eam_compile.c. */

#ifndef EAM_COMPILE_H
#define EAM_COMPILE_H 1
/* C99 */
#include <stdbool.h>
/* internal */
/* includes the BFCompilerError typedef */
#include "eambfc_types.h"

/* Takes 2 open file descriptors - in_fd and out_fd.
 * in_fd is a brainfuck source file, open for reading.
 * out_fd is the destination file, open for writing.
 * It compiles the source code in in_fd, writing the output to out_fd.
 *
 * It does not verify that in_fd and out_fd are valid file descriptors,
 * nor that they are open properly.
 *
 * returns 1 if compilation succeeded.
 * if it runs into a problem, it aborts and returns 0. */
int bfCompile(int in_fd, int out_fd, bool optimize);

/* values for use in printing an error message if compilation fails.*/

extern BFCompilerError err_list[MAX_ERROR];

#endif /* EAM_COMPILE_H */
