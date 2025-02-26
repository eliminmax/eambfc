/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Types used throughout the eambfc codebase. */
#ifndef BFC_TYPES_H
#define BFC_TYPES_H 1
/* C99 */
#include <stdbool.h>
/* POSIX */
#include <sys/types.h>
/* internal */
#include "compat/eambfc_inttypes.h"

typedef unsigned int uint;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* fastest type of AT LEAST the specified size. (i.e. If using a larger type is
 * faster, then a larger type will be used) */
typedef int_fast8_t ifast_8;
typedef int_fast16_t ifast_16;
typedef int_fast32_t ifast_32;
typedef int_fast64_t ifast_64;
typedef uint_fast8_t ufast_8;
typedef uint_fast16_t ufast_16;
typedef uint_fast32_t ufast_32;
typedef uint_fast64_t ufast_64;

typedef struct sized_buf {
    size_t sz; /* size of data used in buffer */
    size_t capacity; /* amount of space allocated for buffer */
    void *buf;
} sized_buf;
#endif /* BFC_TYPES_H */
