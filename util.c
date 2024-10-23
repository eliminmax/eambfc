/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */

/* C99 */
#include <limits.h> /* SSIZE_MAX */
#include <stdlib.h> /* realloc */
#include <string.h> /* memcpy */
/* POSIX */
#include <unistd.h> /* write */
/* internal */
#include "err.h" /* basic_err, alloc_err */
#include "types.h" /* ssize_t, size_t, off_t */


bool write_obj(int fd, const void *buf, size_t ct, off_t *sz) {
    if (ct > SSIZE_MAX) {
        basic_err(
            "WRITE_TOO_LARGE",
            "Didn't write because write is too large to properly validate."
        );
    }
    ssize_t written = write(fd, buf, ct);
    if (written > 0) *sz += written;
    if (written != (ssize_t)ct) {
        basic_err("FAILED_WRITE", "Failed to write to file");
        return false;
    }
    return true;
}

bool append_obj(sized_buf *dst, const void *bytes, size_t bytes_sz) {
    if (dst->sz + bytes_sz > dst->alloc_sz) {
        dst->alloc_sz += 4096;
        void* new_buf = realloc(dst->buf, dst->alloc_sz);
        if (new_buf == NULL) {
            alloc_err();
            free(dst->buf);
            return false;
        }
        dst->buf = new_buf;
    } else if (dst->buf == NULL) {
        return false;
    }
    char* start_addr = dst->buf;
    memcpy(start_addr + dst->sz, bytes, bytes_sz);
    dst->sz += bytes_sz;
    return true;
}
