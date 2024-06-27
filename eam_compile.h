/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file provides an interface to eam_compile.c. */

#ifndef EAM_COMPILE_H
#define EAM_COMPILE_H 1
/* internal */
#include "types.h"

/* Takes 2 open file descriptors - in_fd and out_fd.
 * in_fd is a brainfuck source file, open for reading.
 * out_fd is the destination file, open for writing.
 * It compiles the source code in in_fd, writing the output to out_fd.
 *
 * It does not verify that in_fd and out_fd are valid file descriptors,
 * nor that they are open properly.
 *
 * returns true if compilation succeeded.
 * if it runs into any problems, it appends errors to the error_list, and it
 * returns false. It will try to continue after hitting certain errors, so that
 * the resulting (corrupt) binary can still be examined and debugged. If that is
 * not needed, delete the output file if eambfc returns false.
 *
 * If optimize is set to true, it first converts the contents of in_fd to a tiny
 * intermediate representation (EAMBFC-IR, which is a superset of brainfuck),
 * then compiles that, typically cutting the size of the output code by a decent
 * amount. In some cases the row orcolumn for error messages will be the
 * location in the internal EAMBFC-IR, rather than the source code. */
bool bfCompile(int in_fd, int out_fd, bool optimize);

#endif /* EAM_COMPILE_H */
