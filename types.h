/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Types used throughout the eambfc codebase. */
#ifndef EAMBFC_TYPES_H
#define EAMBFC_TYPES_H 1
/* C99 */
#include <stdbool.h> /* bool, true, false */
/* POSIX */
#include <sys/types.h> /* size_t, ssize_t, off_t */
/* internal */
#include "compat/eambfc_inttypes.h" /* uint*_t, int*_t, PRI*, SCN*, and more */

typedef unsigned int uint;
typedef struct sized_buf {
    size_t sz; /* size of data used in buffer */
    size_t capacity; /* amount of space allocated for buffer */
    void* buf;
} sized_buf;
#endif /* EAMBFC_TYPES_H */
