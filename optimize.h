/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Provides a function that returns EAMBFC IR from an open FD */

#ifndef BFC_OPTIMIZE_H
#define BFC_OPTIMIZE_H 1
/* internal */
#include <attributes.h>
#include <types.h>

/* filter out all non-BF bytes, and anything that is trivially determined to be
 * dead code, or code with no effect (e.g. "+-" or "<>"), and replace "[-]" and
 * "[+]" with "@".
 *
 * "in_name" is the source filename, and is used only to generate error
 * messages. */
nonnull_args bool filter_dead(SizedBuf *bf_code, const char *in_name);
#endif /* BFC_OPTIMIZE_H */
