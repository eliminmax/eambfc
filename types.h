/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Types used throughout the eambfc codebase. */
#ifndef BFC_TYPES_H
#define BFC_TYPES_H 1
/* C99 */
#include <stdbool.h> /* IWYU pragma: export */
#include <stddef.h> /* IWYU pragma: export */
/* POSIX */
#include <sys/types.h> /* IWYU pragma: export */
/* internal */
#include "compat/eambfc_inttypes.h" /* IWYU pragma: export */

typedef unsigned int uint;
typedef unsigned char uchar;
typedef signed char schar;

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

/* A pointer to memory, accompanied by size and capacity information.
 * Functions that take a `sized_buf *` can freely assume that `buf` is not
 * `NULL`, and that either caller ensured enough space was available, or that
 * `buf` can be safely reallocated with `realloc`. If a function returns a
 * `sized_buf`, it should be assumed that the caller is supposed to free it with
 * `free`, and functions should only be passed pointers to sized_bufs. */
typedef struct sized_buf {
    char *buf; /* a buffer of data in memory */
    size_t sz; /* size of data used in buffer */
    size_t capacity; /* amount of space allocated for buffer */
} sized_buf;
#endif /* BFC_TYPES_H */
