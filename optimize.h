/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Provides a function that returns EAMBFC IR from an open FD */

#ifndef EAMBFC_OPTIMIZE_H
#define EAMBFC_OPTIMIZE_H 1
/* internal */
#include "types.h" /* sized_buf */

/* Replaces the content of the buffer with a null-terminated string containing
 * an internal intermediate representation of the code, and dead loops are
 * removed, as are sequences of instructions that cancel out, such as `<>`.
 *
 * SUBSTITUTIONS:
 *
 * N consecutive `>` instructions are replaced with `}N`.
 * N consecutive `<` instructions are replaced with `{N`.
 * N consecutive `+` instructions are replaced with `#N`.
 * N consecutive `-` instructions are replaced with `=N`.
 *
 * single `+`, `-`, `<`, and `>` instructions are left as is.
 *
 * `[+]` and `[-]` both get replaced with `@`.
 *
 * all `,` and `.` instructions are left unchanged, as are any `[` or `]`
 * instructions not part of the two sequences that are replaced with `@`. */
bool filter_dead(sized_buf *src);
#endif /* EAMBFC_OPTIMIZE_H */
