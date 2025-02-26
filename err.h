/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Functions and data storage to handle error messages */
#ifndef BFC_ERR_H
#define BFC_ERR_H 1
/* internal */
#include "attributes.h"
#include "types.h"

typedef enum {
    BF_ERR_APPEND_TO_NULL,
    BF_ERR_BAD_EXTENSION,
    BF_ERR_BUF_TOO_LARGE,
    BF_ERR_FAILED_READ,
    BF_ERR_FAILED_WRITE,
    BF_ERR_ICE_PARAMS_TOO_LONG,
    BF_ERR_IMMEDIATE_TOO_LARGE,
    BF_ERR_INVALID_IR,
    BF_ERR_INVALID_JUMP_ADDRESS,
    BF_ERR_JUMP_TOO_LONG,
    BF_ERR_MGR_ATEXIT_FAILED,
    BF_ERR_MGR_CLOSE_UNKNOWN,
    BF_ERR_MGR_FREE_UNKNOWN,
    BF_ERR_MGR_REALLOC_UNKNOWN,
    BF_ERR_MISSING_OPERAND,
    BF_ERR_MULTIPLE_ARCHES,
    BF_ERR_MULTIPLE_EXTENSIONS,
    BF_ERR_MULTIPLE_OUTPUT_EXTENSIONS,
    BF_ERR_MULTIPLE_TAPE_BLOCK_COUNTS,
    BF_ERR_NO_CMDLINE_ARGS,
    BF_ERR_NO_SOURCE_FILES,
    BF_ERR_NO_TAPE,
    BF_ERR_NOT_NUMERIC,
    BF_ERR_OPEN_R_FAILED,
    BF_ERR_OPEN_W_FAILED,
    BF_ERR_PARAMETER_ERROR_ERROR,
    BF_ERR_TAPE_TOO_LARGE,
    BF_ERR_TOO_MANY_ALLOCS,
    BF_ERR_TOO_MANY_INSTRUCTIONS,
    BF_ERR_TOO_MANY_NESTED_LOOPS,
    BF_ERR_TOO_MANY_OPENS,
    BF_ERR_UNKNOWN_ARCH,
    BF_ERR_UNKNOWN_ARG,
    BF_ERR_UNMATCHED_CLOSE,
    BF_ERR_UNMATCHED_OPEN,
    BF_ERR_WRITE_TOO_LARGE,
} bf_err_id;

typedef struct {
    const char *msg;
    size_t line;
    size_t col;
    bf_err_id err_id;
    char instr;
} bf_comp_err;

typedef nonnull_args void (*err_printer)(bf_comp_err e);

typedef struct {
    err_printer *e;
    sized_buf *msgs;
    bf_comp_err *errs;
    size_t capacity;
    size_t count;
    bool quiet: 1;
    bool json : 1;
} bf_err_list;

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
