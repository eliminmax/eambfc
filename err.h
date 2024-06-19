/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Functions and data storage to handle error messages */
#ifndef EAM_ERR_H
#define EAM_ERR_H 1
/* internal */
#include "eambfc_types.h"

/* TODO: Error handling needs a massive refactor. */

/* list of errors from the current compilation job */
extern BFCompilerError err_list[MAX_ERROR];
/* ugly bad practice global variables used more in eam_compile.c than err.c */

/* the location that the errors occurred. */
extern unsigned int instr_line;
extern unsigned int instr_col;

/* clear the error list for a new compilation job */
void resetErrors(void);

/* add an error to the list of errors */
void appendError(char instr, char *error_msg, char *err_id);

#endif /* EAM_ERR_H */
