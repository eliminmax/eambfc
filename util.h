/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */
#ifndef BFC_UTIL_H
#define BFC_UTIL_H 1
/* C99 */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
/* internal */
#include "attributes.h"
#include "config.h"
#include "err.h"
#include "types.h"

/* Try to `malloc(size)`. On success, pass the returned pointer.
 * On failure, call `alloc_err` */
inline malloc_like nonnull_ret void *checked_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) alloc_err();
    return ptr;
}

/* Try to `realloc(ptr, size)`. On success, pass the returned pointer.
 * On failure, call `alloc_err` */
inline nonnull_ret void *checked_realloc(void *ptr, size_t size) {
    void *newptr = realloc(ptr, size);
    if (!newptr) {
        free(ptr);
        alloc_err();
    }
    return newptr;
}

/* return the number of trailing zeroes in val */
const_fn inline u8 trailing_0s(u64 val) {
    if (!val) return UINT8_MAX;
    u8 counter = 0;
    while (!(val & 1)) {
        val >>= 1;
        counter += 1;
    }
    return counter;
}

/* return `nbytes`, padded to the next multiple of `BFC_CHUNK_SIZE`. If it is
 * already a multiple of `BFC_CHUNK_SIZE`, it is returned as-is, and if the
 * padding would exceed `SIZE_MAX`, it returns `SIZE_MAX`. */
const_fn inline size_t chunk_pad(size_t nbytes) {
    if (!(nbytes & (BFC_CHUNK_SIZE - 1))) return nbytes;
    size_t ret = (nbytes & ~(size_t)(BFC_CHUNK_SIZE - 1)) + BFC_CHUNK_SIZE;
    return (ret < nbytes) ? SIZE_MAX : ret;
}

/* Return true if signed `val` fits within specified number of bits */
const_fn inline bool bit_fits(i64 val, u8 bits) {
    return val >= -(INT64_C(1) << (bits - 1)) &&
           val < (INT64_C(1) << (bits - 1));
}

/* create a new sized_buf with the provided capacity, and an allocated buffer */
inline sized_buf newbuf(size_t sz) {
    sized_buf newbuf = {
        .buf = checked_malloc(sz),
        .sz = 0,
        .capacity = sz,
    };
    return newbuf;
}

/* return the least significant nbits of val sign-extended to 64 bits. */
const_fn inline i64 sign_extend(i64 val, u8 nbits) {
    u8 shift_amount = (sizeof(i64) * 8) - nbits;
    /* shifting into the sign bit is undefined behavior, so cast it to unsigned,
     * then assign it back. */
    i64 lshifted = ((u64)val << shift_amount);
    return lshifted >> shift_amount;
}

/* Attempts to write ct bytes from buf to the specified fd.
 * If all bytes are written, returns true, otherwise, it outputs a FailedWrite
 * error message and returns false. */
nonnull_args bool write_obj(
    int fd, const void *restrict buf, size_t ct, const char *restrict outname
);

/* reserve nbytes bytes at the end of dst, and returns a pointer to the
 * beginning of them - it's assumed that the caller will populate them, so the
 * sized_buf will consider them used */
nonnull_ret void *sb_reserve(sized_buf *sb, size_t nbytes);

/* Appends first bytes_sz of bytes to dst, reallocating dst as needed. */
nonnull_args void append_obj(
    sized_buf *restrict dst, const void *restrict bytes, size_t bytes_sz
);

/* append `str` to `dst` with `append_obj`, and adjust `dst->sz` to exclude the
 * null terminator */
inline void append_str(sized_buf *restrict dst, const char *restrict str) {
    append_obj(dst, str, strlen(str) + 1);
    dst->sz--;
}

/* Reads the contents of fd into a sized_buf. If a read error occurs, frees
 * what's already been read, and sets the sized_buf to {0, 0, NULL}. */
sized_buf read_to_sized_buf(int fd, const char *in_name);

#endif /* BFC_UTIL_H */
