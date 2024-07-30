/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Functions and data storage to handle error messages */
#ifndef EAM_ERR_H
#define EAM_ERR_H 1
/* internal */
#include "types.h" /* uint */

/* enable quiet mode - this does not print error messages to stderr. Does not
 * affect JSON messages printed to stdout. */
void quiet_mode(void);
/* enable JSON display mode - this prints JSON-formatted error messagess to
 * stdout instead of printing human-readable error messages to stderr. */
void json_mode(void);

/* functions to display error messages, depending on the current error mode. */

/* special handling for malloc/realloc failure error messages, which avoids any
 * further use of malloc/realloc for purposes like generating JSON-escaped
 * strings. */
void alloc_err(void);

/* a generic error message */
void basic_err(char* id, char *msg);

/* an error message related to a specific instruction */
void instr_err(char *id, char *msg, char instr);

/* an error message related to a specific instruction at a specific location */
void position_err(char *id, char *msg, char instr, uint line, uint col);

/* replaces first instance of "{}" within proto with arg, then passes it as msg
 * to basic_err */
void param_err(char *id, char *proto, char *arg);

#endif /* EAM_ERR_H */
