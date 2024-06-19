/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Handle error messages */

/* C99 */
#include <stdbool.h>
/* internal */
#include "eambfc_types.h"

/* index of the current error in the error list */
err_index err_ind;

/* location the error was triggered at */
unsigned int instr_line, instr_col;

BFCompilerError err_list[MAX_ERROR];

void resetErrors(void) {
    /* reset error list */
    for(err_index i = 0; i < MAX_ERROR; i++) {
        err_list[i].line = 1;
        err_list[i].col = 0;
        err_list[i].err_id = "";
        err_list[i].err_msg = "";
        err_list[i].instr = '\0';
        err_list[i].active = false;
    }
    err_ind = 0;
}

void appendError(char instr, char *error_msg, char *err_id) {
    uint8_t i = err_ind++;
    /* Ensure i is in bounds; discard errors after MAX_ERROR */
    if (i < MAX_ERROR) {
        err_list[i].err_msg = error_msg;
        err_list[i].err_id = err_id;
        err_list[i].instr = instr;
        err_list[i].line = instr_line;
        err_list[i].col = instr_col;
        err_list[i].active = true;
    }
}
