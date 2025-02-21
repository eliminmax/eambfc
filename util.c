/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */

/* C99 */
#include <limits.h> /* SSIZE_MAX */
#include <string.h> /* memcpy */
/* POSIX */
#include <unistd.h> /* read, write */
/* internal */
#include "err.h" /* basic_err */
#include "resource_mgr.h" /* mgr_malloc, mgr_realloc, mgr_free */
#include "types.h" /* ssize_t, size_t, off_t */

/* Return true if unsigned val fits within bits */
bool bit_fits_u(uintmax_t val, u8 bits) {
    return val < UINTMAX_C(1) << bits;
}

/* Return true if signed val fits within bits */
bool bit_fits_s(intmax_t val, u8 bits) {
    intmax_t max = INTMAX_C(1) << (bits - 1);
    intmax_t min = -max;
    return val >= min && val < max;
}

/* return the least significant bits of val sign-extended */
intmax_t sign_extend(intmax_t val, u8 bits) {
    u8 shift_amount = (sizeof(intmax_t) * 8) - bits;
    /* shifting into the sign bit is undefined behavior, so cast it to unsigned,
     * then assign it back. */
    intmax_t lshifted = ((uintmax_t)val << shift_amount);
    return lshifted >> shift_amount;
}

/* Wrapper around write.3POSIX that returns true if all bytes were written, and
 * prints an error and returns false otherwise or if ct is too large to
 * validate. */
bool write_obj(int fd, const void *buf, size_t ct) {
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

/* Append bytes to dst, handling reallocs as needed.
 * Assumes that dst has been allocated with resource_mgr. */
bool append_obj(sized_buf *dst, const void *bytes, size_t bytes_sz) {
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
