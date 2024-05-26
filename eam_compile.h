/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * */

#ifndef EAM_COMPILE
#define EAM_COMPILE
#include <stdbool.h>

/* Takes 2 open file descriptors - inputFD and outputFD.
 * inputFD is a brainfuck source file, open for reading.
 * outputFS is the destination file, open for writing.
 * It compiles the source code in inputFD, writing the output to outputFD.
 *
 * It does not verify that inputFD and outputFD are valid file descriptors,
 * nor that they are open properly.
 *
 * returns 1 if compilation succeeded.
 * if it runs into a problem, it aborts and returns 0.
 *
 * If keep is set to true, that means that `main` won't delete incomplete
 * executables if the compilation fails, so headers will still be written. */
int bfCompile(int inputFD, int outputFD, bool keep);

/* values for use in printing an error message if compilation fails.*/

typedef struct {
    unsigned int currentInstructionLine;
    unsigned int currentInstructionColumn;
    char *errorId; 
    char *errorMessage;
    char currentInstruction;
    bool active;
} BFCompilerError;

#define MAX_ERROR 32

extern BFCompilerError ErrorList[MAX_ERROR];

#endif /* EAM_COMPILE */
