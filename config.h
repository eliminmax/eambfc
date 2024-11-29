/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A header file containing values that are intended to be configurable */

#ifndef EAMBFC_CONFIG_H
#define EAMBFC_CONFIG_H 1
/* internal */
#include "compat/elf.h" /* EM_* */

/* __BACKENDS__ when adding a backend, add a macro here */
/* set to 0 if you don't want it included in the eambfc build. */
#define EAMBFC_TARGET_ARM64 1
#define EAMBFC_TARGET_S390X 1
#define EAMBFC_TARGET_X86_64 1
/* The target architecture - should be the same as the ELF e_machine value for
 * that architecture for consistency's sake. */
#define EAMBFC_DEFAULT_TARGET EM_X86_64

/* runs some preprocessor validation that the above settings are sane, and
 * defines some macros based on the configured default value */
#define EAMBFC_PREPROC_POST_CONFIG
#include "post_config.h"
#undef EAMBFC_PREPROC_POST_CONFIG

#endif /* EAMBFC_CONFIG_H */
