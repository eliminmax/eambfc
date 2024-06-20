/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Functions and data storage to handle error messages */
#ifndef EAM_ERR_H
#define EAM_ERR_H 1
/* internal */
#include "eambfc_types.h"

/* enable quiet mode - this does not print error messages to stderr. */
void quietMode(void);
/* enable json display mode - this prints JSON-formatted error messagess to
 * stdout instead of printing human-readable error messages to stderr. */
void jsonMode(void);

/* functions to display error messages, depending on the current mode. */
/* a generic error message */
void basicError(char* id, char *msg);
/* an error message related to a specific instruction */
void instructionError(char *id, char *msg, char instr);
/* an error message related to a specific instruction at a specific location */
void positionError(char *id, char *msg, char instr, uint line, uint col);

/* TODO: Error handling needs a massive refactor. */
/* list of errors from the current compilation job */
extern BFCompilerError err_list[MAX_ERROR];
/* ugly bad practice global variables used more in eam_compile.c than err.c */

/* the location that the errors occurred. */
extern uint instr_line;
extern uint instr_col;

/* clear the error list for a new compilation job */
void resetErrors(void);

#endif /* EAM_ERR_H */
