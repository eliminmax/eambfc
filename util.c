/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */

/* POSIX */
#include <unistd.h> /* write */
/* internal */
#include "err.h" /* basic_err */


bool write_obj(int fd, const void *buf, size_t sz) {
    if (write(fd, buf, sz) != sz) {
        basic_err("FAILED_WRITE", "Failed to write to file");
        return false;
    }
    return true;
}
