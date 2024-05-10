/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * */

#ifndef EAM_COMPILE
#define EAM_COMPILE

/* Takes 2 open file descriptors - inputFD and outputFD.
 * inputFD is a brainfuck source file, open for reading.
 * outputFS is the destination file, open for writing.
 * It compiles the source code in inputFD, writing the output to outputFD.
 *
 * It does not verify that inputFD and outputFD are valid file descriptors,
 * nor that they are open properly.
 *
 * returns 1 if compilation succeeded.
 * if it runs into a problem, it aborts and returns 0. */
int bfCompile(int inputFD, int outputFD);

/* values for use in printing an error message if compilation fails.*/
extern char currentInstruction;
extern unsigned int currentInstructionLine, currentInstructionColumn;
extern char *errorMessage;

#endif /* EAM_COMPILE */
