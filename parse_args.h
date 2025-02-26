/*SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#ifndef BFC_PARSE_ARGS
#define BFC_PARSE_ARGS 1
#include "arch_inter.h"

typedef struct {
    const arch_inter *inter;
    const char *ext;
    const char *out_ext;
    u64 tape_blocks;
    /* use bitfield booleans here */
    bool quiet    : 1;
    bool optimize : 1;
    bool keep     : 1;
    bool moveahead: 1;
    bool json     : 1;
} run_cfg;

run_cfg parse_args(int argc, char *argv[]);

#endif /* BFC_PARSE_ARGS */
