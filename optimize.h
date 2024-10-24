/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Provides a function that returns EAMBFC IR from an open FD */

#ifndef EAMBFC_OPTIMIZE_H
#define EAMBFC_OPTIMIZE_H 1
/* internal */
#include "types.h" /* sized_buf */

/* Replaces the content of the buffer with a null-terminated string containing
 * an internal intermediate representation of the code.
 *
 * SUBSTITUTIONS:
 *
 * >*N: }N
 * <*N: {N
 * +*N: #N   | chosen as a "double stroked" version of the
 * -*N: =N   | symbol, not for mathematical meaning.
 *
 * single +, -, <, and > instructions are left as is.
 *
 * [+] and [-] both get replaced with @ */
void to_ir(sized_buf *src);
#endif /* EAMBFC_OPTIMIZE_H */
