/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */
#ifndef BFC_UTIL_H
#define BFC_UTIL_H 1
#include "attributes.h"
#include "types.h"

/* return the number of trailing zeroes in val */
const_fn inline u8 trailing_0s(u64 val) {
    u8 counter = 0;
    while (!(val & 1)) {
        val >>= 1;
        counter += 1;
    }
    return counter;
}

/* Return true if signed `val` fits within specified number of bits */
const_fn inline bool bit_fits(i64 val, u8 bits) {
    int64_t max = INT64_C(1) << (bits - 1);
    int64_t min = -max;
    return val >= min && val < max;
}

/* return the least significant bits of val sign-extended */
const_fn inline i64 sign_extend(i64 val, u8 bits) {
    u8 shift_amount = (sizeof(i64) * 8) - bits;
    /* shifting into the sign bit is undefined behavior, so cast it to unsigned,
     * then assign it back. */
    i64 lshifted = ((u64)val << shift_amount);
    return lshifted >> shift_amount;
}

/* Passes arguments to write, and checks if bytes written is equal to ct.
 * If it is, returns true. otherwise, outputs a FAILED_WRITE error and
 * returns false. If ct is more than SSIZE_MAX, it will print an error and
 * return false immediately, as it's too big to validate.
 *
 * See write.3POSIX for more information on arguments. */
nonnull_args bool write_obj(
    int fd, const void *buf, size_t ct, const char *outname
);

/* reserve nbytes bytes at the end of dst, and returns a pointer to the
 * beginning of them - it's assumed that the caller will populate them, so the
 * sized_buf will consider them used */
nonnull_ret void *sb_reserve(sized_buf *sb, size_t nbytes);

/* Appends first bytes_sz of bytes to dst, reallocating dst as needed. */
nonnull_args bool append_obj(
    sized_buf *dst, const void *bytes, size_t bytes_sz
);

/* Reads the contents of fd into a sized_buf. If a read error occurs, frees
 * what's already been read, and sets the sized_buf to {0, 0, NULL}. */
sized_buf read_to_sized_buf(int fd, const char *in_name);
#endif /* BFC_UTIL_H */
