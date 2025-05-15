/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */
#ifndef BFC_UTIL_H
#define BFC_UTIL_H 1
/* C99 */
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
/* internal */
#include <attributes.h>
#include <config.h>
#include <types.h>

#include "err.h"

#ifdef BFC_UTIL_EXTERN
#define INLINE_DECL(...) \
    extern inline __VA_ARGS__; \
    inline __VA_ARGS__
#else /* BFC_UTIL_EXTERN */
#define INLINE_DECL(...) inline __VA_ARGS__
#endif /* BFC_UTIL_EXTERN */

/* Try to `malloc(size)`. On success, pass the returned pointer.
 * On failure, call `alloc_err` */
INLINE_DECL(malloc_like nonnull_ret void *checked_malloc(size_t size)) {
    void *ptr = malloc(size);
    if (!ptr) alloc_err();
    return ptr;
}

/* Try to `realloc(ptr, size)`. On success, pass the returned pointer.
 * On failure, call `alloc_err` */
INLINE_DECL(nonnull_ret void *checked_realloc(void *ptr, size_t size)) {
    void *newptr = realloc(ptr, size);
    if (!newptr) {
        free(ptr);
        alloc_err();
    }
    return newptr;
}

/* return the number of trailing zeroes in val */
INLINE_DECL(const_fn u8 trailing_0s(umax val)) {
    if (!val) return UINT8_MAX;
    u8 counter = 0;
    while (!(val & 1)) {
        val >>= 1;
        counter += 1;
    }
    return counter;
}

/* trying to convert a value into a signed type that's too large has
 * implementation-defined behavior. On StackOverflow, user743382 implemented a
 * fully-portable way to cast an `int` into an `unsigned int` in C++, and this
 * macro declares functions that adapt the approach used into C99.
 *
 * https://stackoverflow.com/a/13208789
 *
 * The first parameter is the name of the cast function to generate.
 * The second is the unsigned input type.
 * The third is the signed output type.
 * The fourth is the *MIN macro for the output type
 * The fifth is the *MAX macro for the output type
 *
 * Example:
 * `DECL_CAST_N(cast_i64, u64, i64, INT64_MIN, INT64_MAX)` expands to
 * ```
 * inline const_fn i64 cast_i64(u64 val) {
 *     if (val <= (u64)(INT64_MAX)) {
 *         return val;
 *     } else {
 *         return (INT64_MIN) + (i64)(val - (INT64_MIN));
 *     }
 * }
 * ```
 *
 * The default builds of both gcc and clang in Debian Bookworm optimize the
 * resulting functions to the equivalent of `return val`, even at `-O1`, so
 * should inline into a no-op with any modern compiler. */
#define DECL_CAST_N(fn, utype, stype, typemin, typemax) \
    INLINE_DECL(const_fn stype fn(utype val)) { \
        if (val <= (utype)(typemax)) { \
            return val; \
        } else { \
            return (typemin) + (stype)(val - (typemin)); \
        } \
    }

DECL_CAST_N(cast_i64, u64, i64, INT64_MIN, INT64_MAX)
DECL_CAST_N(cast_i32, u32, i32, INT32_MIN, INT32_MAX)
DECL_CAST_N(cast_i16, u16, i16, INT16_MIN, INT16_MAX)

/* not currently needed:  DECL_CAST_N(cast_i8, u8, i8, INT8_MIN, INT8_MAX) */

/* return `nbytes`, padded to the next multiple of `BFC_CHUNK_SIZE`. If it is
 * already a multiple of `BFC_CHUNK_SIZE`, it is returned as-is, and if the
 * padding would exceed `SIZE_MAX`, it returns `SIZE_MAX`. */
INLINE_DECL(const_fn size_t chunk_pad(size_t nbytes)) {
    if (!(nbytes & (BFC_CHUNK_SIZE - 1))) return nbytes;
    size_t ret = (nbytes & ~(size_t)(BFC_CHUNK_SIZE - 1)) + BFC_CHUNK_SIZE;
    return (ret < nbytes) ? SIZE_MAX : ret;
}

/* Return true if signed `val` fits within specified number of bits */
INLINE_DECL(const_fn bool bit_fits(imax val, u8 bits)) {
    return val >= -(INTMAX_C(1) << (bits - 1)) &&
           val < (INTMAX_C(1) << (bits - 1));
}

/* create a new SizedBuf with the provided capacity, and an allocated buffer */
INLINE_DECL(SizedBuf newbuf(size_t sz)) {
    SizedBuf newbuf = {
        .buf = checked_malloc(sz),
        .sz = 0,
        .capacity = sz,
    };
    return newbuf;
}

/* return the least significant nbits of val sign-extended to 64 bits. */
INLINE_DECL(const_fn i64 sign_extend(i64 val, ufast_8 nbits)) {
    assert(nbits < 64);
    return cast_i64((((u64)val) << (64 - nbits))) >> (64 - nbits);
}

/* Attempts to write `ct` bytes from `buf` to `fd`.
 * If all bytes are written, returns `true`, otherwise, it sets `err->id` and
 * `err->msg`, zeroing out other fields in `err`, then returns `false`. */
nonnull_args bool write_obj(
    int fd, const void *restrict buf, size_t ct, BFCError *restrict outname
);

/* reserve nbytes bytes at the end of dst, and returns a pointer to the
 * beginning of them - it's assumed that the caller will populate them, so the
 * SizedBuf will consider them used */
nonnull_ret void *sb_reserve(SizedBuf *sb, size_t nbytes);

/* Appends first bytes_sz of bytes to dst, reallocating dst as needed. */
nonnull_args void append_obj(
    SizedBuf *restrict dst, const void *restrict bytes, size_t bytes_sz
);

/* append `str` to `dst` with `append_obj`, and adjust `dst->sz` to exclude the
 * null terminator */
INLINE_DECL(void append_str(SizedBuf *restrict dst, const char *restrict str)) {
    append_obj(dst, str, strlen(str) + 1);
    dst->sz--;
}

union read_result {
    SizedBuf sb;
    BFCError err;
};

/* Tries to read `fd` into `result->sb`. If a read fails, returns `false`, and
 * sets `result->err` to an error with the `id` and `msg` set. Once no data is
 * left to read, it returns `true`. */
nonnull_args bool read_to_sb(int fd, union read_result *result);

#undef INLINE_DECL

#endif /* BFC_UTIL_H */
