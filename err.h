/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Functions and data storage to handle error messages */
#ifndef BFC_ERR_H
#define BFC_ERR_H 1
/* internal */
#include <attributes.h>
#include <types.h>

typedef enum {
    BF_NOT_ERR,
    BF_ERR_BAD_EXTENSION,
    BF_ERR_BUF_TOO_LARGE,
    BF_ERR_CODE_TOO_LARGE,
    BF_ERR_FAILED_READ,
    BF_ERR_FAILED_WRITE,
    BF_ERR_INPUT_IS_OUTPUT,
    BF_ERR_JUMP_TOO_LONG,
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
    BF_ERR_UNKNOWN_ARCH,
    BF_ERR_UNKNOWN_ARG,
    BF_ERR_UNMATCHED_CLOSE,
    BF_ERR_UNMATCHED_OPEN,
    /* ICE divider */
    BF_ICE_IMMEDIATE_TOO_LARGE,
    BF_ICE_INVALID_IR,
    BF_ICE_INVALID_JUMP_ADDRESS,
    /* AllocFailure divider */
    BF_FATAL_ALLOC_FAILURE,
} bf_err_id;

typedef enum {
    OUTMODE_QUIET = 0,
    OUTMODE_NORMAL = 1,
    OUTMODE_JSON = 2,
} out_mode;

typedef struct {
    size_t line;
    size_t col;
} src_loc;

typedef struct {
    /* error message text */
    union errmsg {
        const char *ref;
        char *alloc;
    } msg;

    /* file error occurred in. May be NULL. */
    const char *file;

    /* position in file of error - if `has_location` is false, may have an
     * uninitialized value. */
    src_loc location;

    /* character in file that error occurred with - if `has_instr` is false, may
     * have an uninitialized value. */
    char instr;
    /* The error ID code for the error */
    bf_err_id id: 12;

    /* set to true if `instr` is specified */
    bool has_instr   : 1;
    /* set to true if `location` is specified */
    bool has_location: 1;
    /* set to true if `msg` is allocated */
    bool is_alloc    : 1;
} bf_comp_err;

/* enable quiet mode - this does not print error messages to stderr. Does not
 * affect JSON messages printed to stdout. */
void quiet_mode(void);
/* enable JSON display mode - this prints JSON-formatted error messagess to
 * stdout instead of printing human-readable error messages to stderr. */
void json_mode(void);

/* function to display error messages, depending on the current error mode. */
void display_err(bf_comp_err e);

nonnull_args inline bf_comp_err basic_err(bf_err_id id, const char *msg) {
    return (bf_comp_err){
        .id = id,
        .msg.ref = msg,
    };
}

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
