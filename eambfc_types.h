/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains typedefs used throughout eambfc. */

#ifndef EAMBFC_TYPES_H
#define EAMBFC_TYPES_H 1

/* internal */
#include "compat/eambfc_inttypes.h"
#include "config.h"

/* ensure that an appropriate type is used for error index */
#if MAX_ERROR <= UINT8_MAX
typedef uint8_t err_index;
#elif MAX_ERROR <= UINT16_MAX
typedef uint16_t err_index;
#elif MAX_ERROR <= UINT32_MAX
typedef uint32_t err_index;
#else
typedef uint64_t err_index;
#endif

/* ensure that an appropriate type is used for jump stack index */
#if MAX_ERROR <= INT8_MAX
typedef int8_t jump_index;
#elif MAX_ERROR <= INT16_MAX
typedef int16_t jump_index;
#elif MAX_ERROR <= INT32_MAX
typedef int32_t jump_index;
#else
typedef int64_t jump_index;
#endif

typedef unsigned int uint;

typedef struct {
    unsigned int line;
    unsigned int col;
    char *err_id;
    char *err_msg;
    char instr;
    bool active;
} BFCompilerError;

#endif /* EAMBFC_TYPES_H */
