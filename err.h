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
    BF_ERR_BAD_EXTENSION,
    BF_ERR_BUF_TOO_LARGE,
    BF_ERR_FAILED_READ,
    BF_ERR_FAILED_WRITE,
    BF_ERR_INPUT_IS_OUTPUT,
    BF_ERR_JUMP_TOO_LONG,
    BF_ERR_MGR_ATEXIT_FAILED,
    BF_ERR_MISSING_OPERAND,
    BF_ERR_MULTIPLE_ARCHES,
    BF_ERR_MULTIPLE_EXTENSIONS,
    BF_ERR_MULTIPLE_OUTPUT_EXTENSIONS,
    BF_ERR_MULTIPLE_TAPE_BLOCK_COUNTS,
    BF_ERR_NESTED_TOO_DEEP,
    BF_ERR_NO_SOURCE_FILES,
    BF_ERR_OPEN_R_FAILED,
    BF_ERR_OPEN_W_FAILED,
    BF_ERR_TAPE_SIZE_NOT_NUMERIC,
    BF_ERR_TAPE_SIZE_ZERO,
    BF_ERR_TAPE_TOO_LARGE,
    BF_ERR_TOO_MANY_INSTRUCTIONS,
    BF_ERR_UNKNOWN_ARCH,
    BF_ERR_UNKNOWN_ARG,
    BF_ERR_UNMATCHED_CLOSE,
    BF_ERR_UNMATCHED_OPEN,
    BF_ERR_WRITE_TOO_LARGE,
    /* ICE divider */
    BF_ICE_APPEND_TO_NULL,
    BF_ICE_IMMEDIATE_TOO_LARGE,
    BF_ICE_INVALID_IR,
    BF_ICE_INVALID_JUMP_ADDRESS,
    BF_ICE_MGR_CLOSE_UNKNOWN,
    BF_ICE_MGR_FREE_UNKNOWN,
    BF_ICE_MGR_REALLOC_UNKNOWN,
    BF_ICE_PARAMS_TOO_LONG,
    BF_ICE_TOO_MANY_ALLOCS,
    BF_ICE_TOO_MANY_OPENS,
    /* AllocFailure divider */
    BF_FATAL_ALLOC_FAILURE,
} bf_err_id;

typedef enum {
    OUTMODE_QUIET = 0,
    OUTMODE_NORMAL = 1,
    OUTMODE_JSON = 2,
} out_mode;

typedef struct {
    const char *msg;
    const char *file;
    size_t line;
    size_t col;
    bf_err_id id;
    char instr;
    bool has_instr   : 1;
    bool has_location: 1;
} bf_comp_err;

/* enable quiet mode - this does not print error messages to stderr. Does not
 * affect JSON messages printed to stdout. */
void quiet_mode(void);
/* enable JSON display mode - this prints JSON-formatted error messagess to
 * stdout instead of printing human-readable error messages to stderr. */
void json_mode(void);

/* function to display error messages, depending on the current error mode. */
void display_err(bf_comp_err e);

/* special handling for malloc/realloc failure error messages, which avoids any
 * further use of malloc/realloc for purposes like generating JSON-escaped
 * strings.
 * calls exit(EXIT_FAILURE) */
noreturn void alloc_err(void);

/* an alternative to basic_err that marks error as an internal compiler error,
 * for use when an error can only trigger if another bug was mishandled.
 * Calls exit(EXIT_FAILURE) */
noreturn nonnull_args void internal_err(bf_err_id err_id, const char *msg);

#endif /* BFC_ERR_H */
