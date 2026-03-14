/*SPDX-FileCopyrightText: 2025 - 2026 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#ifndef BFC_PARSE_ARGS
#define BFC_PARSE_ARGS 1
/* C99 */
#include <stdio.h>
/* internal */
#include <types.h>

#include "arch_inter.h"
#include "err.h"

typedef struct {
    enum { STANDARD_RUN, SHOW_VERSION, SHOW_HELP, LIST_TARGETS } run_type;

    OutMode out_mode;
    ArchInter *restrict backend;
    const char *source_extension;
    const char *output_extension;
    u64 tape_blocks;
    size_t nfiles;
    char **files;
    bool optimize;
    bool keep;
    bool continue_on_error;

} RunConfig;

typedef enum {
    ARGS_OK,

#define DEFINE_ARGS_ERR
#include "arg_parse_errs.h"
#undef DEFINE_ARGS_ERR
} ArgParseOutcome;

typedef union {
    const char *str;
    const char *str2[2];
    char unknown_short_opt;
    u64 tape_sizes[2];

    struct tape_too_large {
        u64 tape_blocks;

        enum bit_count { BITS_32 = 32, BITS_64 = 64 } bits;
    } tape_too_large;
} ArgParseErrPayload;

typedef struct {
    const ArchInter *inter;
    const char *ext;
    const char *out_ext;
    u64 tape_blocks;
    bool keep        : 1;
    bool cont_on_fail: 1;
    bool optimize    : 1;
} RunCfg;

typedef union {
    RunConfig ok;
    ArgParseErrPayload err;
} ArgParseOut;

nonnull_args ArgParseOutcome
parse_args(int argc, char *argv[], ArgParseOut *out);

void show_help(const char *progname, FILE *stream);
void show_version(const char *progname);
void list_targets(void);

#endif /* BFC_PARSE_ARGS */
