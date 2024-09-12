/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */

/* C99 */
#include <limits.h> /* SSIZE_MAX */
/* POSIX */
#include <unistd.h> /* write, ssize_t */
/* internal */
#include "err.h" /* basic_err */


bool write_obj(int fd, const void *buf, size_t sz) {
    if (sz > SSIZE_MAX) {
        basic_err(
            "WRITE_TOO_LARGE",
            "Didn't write because write is too large to properly validate."
        );
    }
    if (write(fd, buf, sz) != (ssize_t)sz) {
        basic_err("FAILED_WRITE", "Failed to write to file");
        return false;
    }
    return true;
}
