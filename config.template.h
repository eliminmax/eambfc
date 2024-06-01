/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A header file containing values that are intended to be configurable */

#ifndef EAM_CONFIG_H
#define EAM_CONFIG_H 1

/* Maximum number of errors to store before discarding them */
#define MAX_ERROR @@

/* Tape size in 4096-byte blocks */
/* the tape size in Urban Müller's original implementation, and the de facto
 * minimum tape size for a "proper" implementation, is 30,000. I increased that
 * to the nearest multiple of 4096 (i.e. 32687, which is 8*4096) as the default.
 * I would not recommend decreasing it. */
#define TAPE_BLOCKS @@

/* The current version */
#define EAMBFC_VERSION @@

#endif /* EAM_CONFIG_H */
