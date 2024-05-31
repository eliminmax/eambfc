/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * */

#ifndef EAM_COMPILE_H
#define EAM_COMPILE_H 1
#include <stdbool.h>

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
int bfCompile(int in_fd, int out_fd);

/* values for use in printing an error message if compilation fails.*/

typedef struct {
    unsigned int line;
    unsigned int col;
    char *err_id;
    char *err_msg;
    char instr;
    bool active;
} BFCompilerError;

#define MAX_ERROR 32

extern BFCompilerError err_list[MAX_ERROR];

#endif /* EAM_COMPILE_H */
