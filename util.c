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

#define WOULD_OVERFLOW(val) ((val) >= SIZE_MAX - 4096)
/* Append bytes to dst, handling reallocs as needed.
 * If reallocation fails, free dst->buf then set dst to {0, 0, NULL} */
bool append_obj(sized_buf *dst, const void *bytes, size_t bytes_sz) {
    /* if inadequate space is available, allocate more */
    if (dst->sz + bytes_sz > dst->capacity) {
        size_t added_sz = (bytes_sz & 0xfff) ?
            ((bytes_sz & (~0xfff)) + 0x1000) :
            bytes_sz;
        if (WOULD_OVERFLOW(added_sz) || WOULD_OVERFLOW(dst->sz + added_sz)) {
            basic_err(
                "BUF_SIZE_OVERFLOW",
                "Extending buffer would cause size to overflow."
            );
            goto failure_cleanup;

        }

        dst->capacity += added_sz;
        /* reallocate to new capacity */
        void* new_buf = realloc(dst->buf, dst->capacity);
        if (new_buf == NULL) {
            alloc_err();
            goto failure_cleanup;
        }
        dst->buf = new_buf;
    } else if (dst->buf == NULL) {
        /* if passed a sized_buf with a NULL pointer, exit immediately. */
        return false;
    }

    /* actually append the object now that prep work is done */
    char* start_addr = dst->buf;
    memcpy(start_addr + dst->sz, bytes, bytes_sz);
    dst->sz += bytes_sz;
    return true;

    /* jump here if something went wrong, after calling appopriate *_err
     * function */
failure_cleanup:
    free(dst->buf);
    dst->capacity = 0;
    dst->sz = 0;
    dst->buf = NULL;
    return false;
}

/* Reads the contents of fd into sb. If a read error occurs, frees what's
 * already been read, and sets sb to {0, 0, NULL}. */
void read_to_sized_buf(sized_buf *sb, int fd) {
    sb->sz = 0;
    sb->capacity = 4096;
    sb->buf = malloc(4096);
    if (sb->buf == NULL) {
        alloc_err();
        return;
    }
    /* read into sb in 4096-byte chunks */
    char chunk[4096];
    ssize_t count;
    while ((count = read(fd, &chunk, 4096))) {
        if (count < 0) {
            basic_err("FAILED_READ", "Failed to read from file");
            free(sb->buf);
            sb->sz = 0;
            sb->capacity = 0;
            sb->buf = NULL;
            return;
        }

        /* in case of error, */
        if (!append_obj(sb, &chunk, count)) {
            sb->sz = 0;
            sb->capacity = 0;
            return;
        }
    }
}
