/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Provides a function that returns EAMBFC IR from an open FD */

#ifndef BFC_OPTIMIZE_H
#define BFC_OPTIMIZE_H 1
/* internal */
#include "attributes.h"
#include "types.h"

/* Reads the content of the file fd, and returns a string containing optimized
 * internal intermediate representation of that file's code.
 * fd must be open for reading already, no check is performed.
 * Calling function is responsible for `mgr_free`ing the returned string. */
nonnull_args bool filter_dead(sized_buf *bf_code, const char *in_name);
#endif /* BFC_OPTIMIZE_H */
