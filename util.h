/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */
#ifndef BFC_UTIL_H
#define BFC_UTIL_H 1
#include "attributes.h"
#include "resource_mgr.h"
#include "types.h"

/* by defining BFC_UTIL_C in util.c, needing duplicate definitions of
 * these functions is avoided */
#ifdef BFC_UTIL_C
#define inline_impl extern
#else
#define inline_impl inline
#endif /* BFC_UTIL_C */

/* return the number of trailing zeroes in val */
const_fn inline_impl u8 trailing_0s(u64 val) {
    if (!val) return UINT8_MAX;
    u8 counter = 0;
    while (!(val & 1)) {
        val >>= 1;
        counter += 1;
    }
    return counter;
}

/* Return true if signed `val` fits within specified number of bits */
const_fn inline_impl bool bit_fits(i64 val, u8 bits) {
    return val >= -(INT64_C(1) << (bits - 1)) &&
           val < (INT64_C(1) << (bits - 1));
}

/* create a new sized_buf with the provided size, and a buffer allocated through
 * resource_mgr */
inline_impl sized_buf newbuf(size_t sz) {
    sized_buf newbuf = {
        .buf = mgr_malloc(sz),
        .sz = 0,
        .capacity = sz,
    };
    return newbuf;
}

/* return the least significant bits of val sign-extended */
const_fn inline_impl i64 sign_extend(i64 val, u8 bits) {
    u8 shift_amount = (sizeof(i64) * 8) - bits;
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
nonnull_args bool append_obj(
    sized_buf *restrict dst, const void *restrict bytes, size_t bytes_sz
);

/* Reads the contents of fd into a sized_buf. If a read error occurs, frees
 * what's already been read, and sets the sized_buf to {0, 0, NULL}. */
sized_buf read_to_sized_buf(int fd, const char *in_name);

#undef inline_impl
#endif /* BFC_UTIL_H */
