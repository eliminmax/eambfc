/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Functions and data storage to handle error messages */
#ifndef EAM_ERR_H
#define EAM_ERR_H 1
/* internal */
#include "eambfc_types.h"


extern BFCompilerError err_list[MAX_ERROR];
extern unsigned int instr_line;
extern unsigned int instr_col;

void resetErrors(void);
void appendError(char instr, char *error_msg, char *err_id);

#endif /* EAM_ERR_H */
