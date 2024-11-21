/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */
#ifndef EAMBFC_UTIL_H
#define EAMBFC_UTIL_H 1
#include "types.h" /* off_t, size_t, sized_buf */
/* Passes arguments to write, and checks if bytes written is equal to ct.
 * If it is, returns true. otherwise, outputs a FAILED_WRITE error and
 * returns false. If ct is more than SSIZE_MAX, it will print an error and
 * return false immediately, as it's too big to validate.
 *
 * See write.3POSIX for more information on arguments. */
bool write_obj(int fd, const void *buf, size_t ct);

/* Appends first bytes_sz of bytes to dst, reallocating dst as needed. */
bool append_obj(sized_buf *dst, const void *bytes, size_t bytes_sz);

/* Reads the contents of fd into a sized_buf. If a read error occurs, frees
 * what's already been read, and sets the sized_buf to {0, 0, NULL}. */
sized_buf read_to_sized_buf(int fd);
#endif /* EAMBFC_UTIL_H */
