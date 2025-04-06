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
#include <stdint.h> /* IWYU pragma: export */

typedef unsigned int uint;
typedef unsigned char uchar;
typedef signed char schar;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* 64-bit sized types are not required by POSIX for some reason, so fall back to
 * using [u]int_least64_t, which POSIX does require. If "INT_TORTURE_TEST" is
 * defined, instead use the __int128 type (a GCC extension), and its unsigned
 * counterpart, as a means to ensure that no errors would occur if using
 * `[u]int_least64_t` in place of `[u]int64_t` */
#ifndef INT_TORTURE_TEST
typedef int64_t i64;
typedef uint64_t u64;
#else /* INT_TORTURE_TEST */
typedef __int128 i64;
typedef unsigned __int128 u64;
#endif /* INT_TORTURE_TEST */

#ifndef INT64_MAX
#define INT64_MIN -9223372036854775808LL
#define INT64_MAX 9223372036854775807LL
#endif /* INT64_MAX */

#ifndef UINT64_MAX
#define UINT64_MAX 18446744073709551615ULL
#endif /* UINT64_MAX */

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
    char *restrict buf; /* a buffer of data in memory */
    size_t sz; /* size of data used in buffer */
    size_t capacity; /* amount of space allocated for buffer */
} sized_buf;
#endif /* BFC_TYPES_H */
