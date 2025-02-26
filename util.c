/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */

/* C99 */
#include <limits.h>
#include <string.h>
/* POSIX */
#include <unistd.h>
/* internal */
#include "attributes.h"
#include "err.h"
#include "resource_mgr.h"
#include "types.h"

/* return the number of trailing zeroes in val */
extern const_fn u8 trailing_0s(u64 val) {
    u8 counter = 0;
    while (!(val & 1)) {
        val >>= 1;
        counter += 1;
    }
    return counter;
}

/* Return true if signed `val` fits within specified number of bits */
extern const_fn bool bit_fits(i64 val, u8 bits) {
    int64_t max = INT64_C(1) << (bits - 1);
    int64_t min = -max;
    return val >= min && val < max;
}

/* return the least significant bits of val sign-extended */
const_fn i64 sign_extend(i64 val, u8 bits) {
    u8 shift_amount = (sizeof(i64) * 8) - bits;
    /* shifting into the sign bit is undefined behavior, so cast it to unsigned,
     * then assign it back. */
    i64 lshifted = ((u64)val << shift_amount);
    return lshifted >> shift_amount;
}

/* Wrapper around write.3POSIX that returns true if all bytes were written, and
 * prints an error and returns false otherwise or if ct is too large to
 * validate. */
nonnull_args bool write_obj(int fd, const void *buf, size_t ct) {
    if (ct > SSIZE_MAX) {
        basic_err(
            "WRITE_TOO_LARGE",
            "Didn't write because write is too large to properly validate."
        );
        return false;
    }
    ssize_t written = write(fd, buf, ct);
    if (written != (ssize_t)ct) {
        basic_err("FAILED_WRITE", "Failed to write to file");
        return false;
    }
    return true;
}

/* reserve nbytes bytes at the end of dst, and returns a pointer to the
 * beginning of them - it's assumed that the caller will populate them, so the
 * sized_buf will consider them used */
nonnull_ret void *sb_reserve(sized_buf *sb, size_t nbytes) {
    if (sb->buf == NULL) {
        internal_err(
            "APPEND_OBJ_TO_NULL", "sb_reserve called with dst->buf set to NULL"
        );
        /* will never return, as internal_err calls exit(EXIT_FAILURE) */
    }
    /* if more space is needed, ensure no overflow occurs when calculating new
     * space requirements, then allocate it. */
    if (sb->sz + nbytes > sb->capacity) {
        /* will reallocate with 0x1000 to 0x2000 bytes of extra space */
        size_t needed_cap = (sb->sz + nbytes + 0x1000) & (~0xfff);
        if (needed_cap < sb->capacity) {
            basic_err(
                "BUF_TOO_LARGE",
                "Out of room, but extending buffer would cause overflow"
            );
            mgr_free(sb->buf);
            sb->capacity = 0;
            sb->sz = 0;
            sb->buf = NULL;
            alloc_err();
        }
        /* reallocate to new capacity */
        sb->buf = mgr_realloc(sb->buf, needed_cap);
        sb->capacity = needed_cap;
    }
    sb->sz += nbytes;
    return (char *)sb->buf + sb->sz - nbytes;
}

/* Append bytes to dst, handling reallocs as needed.
 * Assumes that dst has been allocated with resource_mgr. */
nonnull_args bool append_obj(
    sized_buf *dst, const void *bytes, size_t bytes_sz
) {
    /* if more space is needed, ensure no overflow occurs when calculating new
     * space requirements, then allocate it.
     *
     * Make sure to leave 8 KiB shy of SIZE_MAX available - it will never
     * get anywhere near that high in any realisitic scenario, and the extra
     * space simplifies overflow checking logic. Besides, any sensible malloc
     * implementation will be returning NULL well before this is relevant. */
    if ((bytes_sz > (SIZE_MAX - 0x8000)) ||
        (dst->capacity > (SIZE_MAX - (bytes_sz + 0x8000)))) {
        basic_err(
            "BUF_TOO_LARGE",
            "Extending buffer would put size within 8 KiB of SIZE_MAX"
        );
        mgr_free(dst->buf);
        dst->capacity = 0;
        dst->sz = 0;
        dst->buf = NULL;
        return false;
    }

    if (dst->buf == NULL) {
        internal_err(
            "APPEND_OBJ_TO_NULL", "append_obj called with dst->buf set to NULL"
        );
        /* will never return, as internal_err calls exit(EXIT_FAILURE) */
        return false;
    }
    /* how much capacity is needed */
    size_t needed_cap = bytes_sz + dst->sz;
    /* if needed_cap isn't a multiple of 4 KiB in size, pad it out -
     * most usage of this function is going to be for small objects, so the
     * number of reallocations is vastly reduced that way.
     *
     * Because the previous check established that there's at least 8 KiB of
     * padding available, this is guaranteed not to overflow. */
    if (needed_cap & 0xfff) needed_cap = (needed_cap + 0x1000) & (~0xfff);

    if (needed_cap > dst->capacity) {
        /* reallocate to new capacity */
        dst->buf = mgr_realloc(dst->buf, needed_cap);
        dst->capacity = needed_cap;
    }
    /* actually append the object now that prep work is done */
    memcpy((char *)(dst->buf) + dst->sz, bytes, bytes_sz);
    dst->sz += bytes_sz;
    return true;
}

/* Reads the contents of fd into sb. If a read error occurs, frees what's
 * already been read, and sets sb to {0, 0, NULL}. */
sized_buf read_to_sized_buf(int fd) {
    sized_buf sb = {.sz = 0, .capacity = 4096, .buf = mgr_malloc(4096)};
    /* read into sb in 4096-byte chunks */
    char chunk[4096];
    ssize_t count;
    while ((count = read(fd, &chunk, 4096))) {
        if (count >= 0) {
            append_obj(&sb, &chunk, count);
        } else {
            basic_err("FAILED_READ", "Failed to read from file");
            mgr_free(sb.buf);
            sb.sz = 0;
            sb.capacity = 0;
            sb.buf = NULL;
        }
    }
    return sb;
}
