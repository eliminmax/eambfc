/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A header file containing values that are intended to be configurable */

#ifndef EAMBFC_CONFIG_H
#define EAMBFC_CONFIG_H 1
/* internal */
#include "compat/elf.h" /* EM_* */

/* __BACKENDS__ */
/* for each optional target, set to 0 if you don't want it included in the
 * eambfc binary. */
#define EAMBFC_TARGET_ARM64 1
#define EAMBFC_TARGET_S390X 0
/* The target architecture - should be the same as the ELF e_machine value for
 * that architecture for consistency's sake. */
#define EAMBFC_TARGET EM_X86_64

/* The current version - this is set in the config.h make target */
#define EAMBFC_VERSION @@

/* The current git commit - this is set in the config.h make target */
#define EAMBFC_COMMIT @@

#endif /* EAMBFC_CONFIG_H */
