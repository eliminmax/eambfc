/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Types used throughout the eambfc codebase. */
#ifndef EAMBFC_UINT
#define EAMBFC_UINT 1
/* C99 */
#include <stdbool.h>
/* internal */
/* wrapper for inttypes.h that provides handling for missing uint64_t or int64_t
 * types not guaranteed to be available in the POSIX.1-2008 or C99 standards.
 *
 * used in place of stdint.h or inttypes.h */
#include "compat/eambfc_inttypes.h"

typedef unsigned int uint;
#endif /* EAMBFC_UINT */
