/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A header file containing values that are intended to be configurable */

#ifndef EAM_CONFIG_H
#define EAM_CONFIG_H 1

/* Maximum number of errors to store before discarding them */
#define MAX_ERROR 32
/* ensure that an appropriate type is used for error index */
#if MAX_ERROR <= UINT8_MAX
typedef uint8_t err_index_t;
#elif MAX_ERROR <= UINT16_MAX
typedef uint16_t err_index_t;
#elif MAX_ERROR <= UINT32_MAX
typedef uint32_t err_index_t;
#else
typedef uint64_t err_index_t;
#endif

/* Tape size */
/* the tape size in Urban Müller's original implementation, and the de facto
 * minimum tape size for a "proper" implementation, is 30,000. I increased that
 * to the nearest power of 2 (namely 32768), because I can, and it's cleaner. */
#define TAPE_SIZE 32768
#endif /* EAM_CONFIG_H */
