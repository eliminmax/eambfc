/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */
#ifndef EAMBFC_UTIL_H
#define EAMBFC_UTIL_H 1
#include "types.h" /* off_t, size_t, sized_buf */
/* passes arguments to write, and checks if bytes written is equal to sz.
 * if it is, returns true. otherwise, outputs a FAILED_WRITE error and
 * returns false. See write.3POSIX for more information. */
bool write_obj(int fd, const void *buf, size_t ct);

/* appends bytes to dst, handling reallocs and alloc failures as needed */
bool append_obj(sized_buf *dst, const void *bytes, size_t bytes_sz);
#endif /* EAMBFC_UTIL_H */
