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
#include <unistd.h> /* read, write */
/* internal */
#include "err.h" /* basic_err, alloc_err */
#include "types.h" /* ssize_t, size_t, off_t */


bool write_obj(int fd, const void *buf, size_t ct) {
    if (ct > SSIZE_MAX) {
        basic_err(
            "WRITE_TOO_LARGE",
            "Didn't write because write is too large to properly validate."
        );
    }
    ssize_t written = write(fd, buf, ct);
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

void read_to_sized_buf(sized_buf *sb, int fd) {
    sb->sz = 0;
    sb->alloc_sz = 4096;
    sb->buf = malloc(4096);
    if (sb->buf == NULL) {
        alloc_err();
        return;
    }

    char chunk[4096];
    ssize_t count;
    while ((count = read(fd, &chunk, 4096))) {
        if (count < 0) {
            basic_err("FAILED_READ", "Failed to read from file");
            free(sb->buf);
            sb->sz = 0;
            sb->alloc_sz = 0;
            sb->buf = NULL;
            return;
        }
        if (!append_obj(sb, &chunk, count)) {
            sb->sz = 0;
            sb->alloc_sz = 0;
            return;
        }
    }
}
