/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Functions and data storage to handle error messages */
#ifndef BFC_ERR_H
#define BFC_ERR_H 1
/* internal */
#include "attributes.h"
#include "types.h" /* uint */

/* enable quiet mode - this does not print error messages to stderr. Does not
 * affect JSON messages printed to stdout. */
void quiet_mode(void);
/* enable JSON display mode - this prints JSON-formatted error messagess to
 * stdout instead of printing human-readable error messages to stderr. */
void json_mode(void);

/* functions to display error messages, depending on the current error mode. */

/* print a generic error message */
nonnull_args void basic_err(const char *id, const char *msg);
/* an error message related to a specific instruction */
nonnull_args void instr_err(const char *id, const char *msg, char instr);

/* an error message related to a specific instruction at a specific location */
nonnull_args void position_err(
    const char *id, const char *msg, char instr, uint line, uint col
);

/* replaces first instance of "{}" within proto with arg, then passes it as msg
 * to basic_err */
nonnull_args void param_err(const char *id, const char *proto, const char *arg);

/* FATAL ERRORS
 * these each call exit(EXIT_FAILURE) after printing the message. */

/* special handling for malloc/realloc failure error messages, which avoids any
 * further use of malloc/realloc for purposes like generating JSON-escaped
 * strings. */
noreturn void alloc_err(void);

/* an alternative to basic_err that marks error as an internal compiler error,
 * for use when an error can only trigger if another bug was mishandled.
 * Calls exit(EXIT_FAILURE) */
noreturn nonnull_args void internal_err(const char *id, const char *msg);

#endif /* BFC_ERR_H */
