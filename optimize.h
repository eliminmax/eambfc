/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Provides a function that returns EAMBFC IR from an open FD */

#ifndef EAM_OPTIMIZE_H
#define EAM_OPTIMIZE_H 1

/* Reads the content of the file fd, and returns a string containing optimized
 * internal intermediate representation of that file's code.
 * fd must be open for reading already, no check is performed.
 * Calling function is responsible for `free`ing the returned string.
 *
 * If it fails to allocate space for the string, it returns NULL. */
char *to_ir(int fd);
#endif /* EAM_OPTIMIZE_H */
