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

/* Append bytes to dst, handling reallocs as needed.
 * If realloc fails, frees dst->buf then sets dst to {0, 0, NULL} */
bool append_obj(sized_buf *dst, const void *bytes, size_t bytes_sz) {
    /* if more space is needed, ensure no overflow occurs then allocate it
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
        goto append_obj_cleanup;
    }

    if (dst->buf == NULL) {
        internal_err(
            "APPEND_OBJ_TO_NULL", "append_obj called with dst->buf set to NULL"
        );
        return false;
    }
    /* how much capacity should be allocated */
    size_t needed_cap = bytes_sz + dst->sz;
    /* if needed_cap isn't a multiple of 4 KiB in size, pad it out -
     * most usage of this function is going to be for small objects, so the
     * number of reallocations is vastly reduced that way.
     *
     * Because the previous check established that there's at least 2 KiB of
     * padding available, this is guaranteed not to overflow. */
    if (needed_cap & 0xfff) needed_cap = (needed_cap + 0x1000) & (~0xfff);

    if (needed_cap > dst->capacity) {
        /* reallocate to new capacity */
        void *new_buf = realloc(dst->buf, needed_cap);
        if (new_buf == NULL) {
            alloc_err();
            goto append_obj_cleanup;
        }
        dst->capacity = needed_cap;
        dst->buf = new_buf;
    }
    /* actually append the object now that prep work is done */
    memcpy((char *)(dst->buf) + dst->sz, bytes, bytes_sz);
    dst->sz += bytes_sz;
    return true;

    /* when errors occur, goto this label after calling the appropriate error
     * message for the shared failure-handling steps. */
append_obj_cleanup:
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
