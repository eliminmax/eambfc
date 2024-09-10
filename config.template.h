/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A header file containing values that are intended to be configurable */

#ifndef EAMBFC_CONFIG_H
#define EAMBFC_CONFIG_H 1

/* internal */
#include "compat/elf.h"

/* The target architecture - should be the same as the elf e_machine value for
 * that architecture, to simplify implementation */
#define TARGET_ARCH EM_X86_64

/* The current version */
#define EAMBFC_VERSION @@

/* The current git commit */
#define EAMBFC_COMMIT @@

#endif /* EAMBFC_CONFIG_H */
