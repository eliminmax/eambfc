/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A header file containing values that are intended to be configurable */

#ifndef BFC_CONFIG_H
#define BFC_CONFIG_H 1
/* internal */
#include "compat/elf.h"

/* whether or not to support GNU argument parsing - requires _GNU_SOURCE to be
 * defined if enabled */
#ifndef BFC_LONGOPTS
#define BFC_LONGOPTS 0
#endif /* BFC_LONGOPTS */

/* __BACKENDS__ when adding a backend, add a macro here */
/* set to 0 if you don't want it included in the eambfc build. */
#ifndef BFC_TARGET_ARM64
#define BFC_TARGET_ARM64 1
#endif /* BFC_TARGET_ARM64 */
#ifndef BFC_TARGET_RISCV64
#define BFC_TARGET_RISCV64 1
#endif /* BFC_TARGET_RISCV64 */
#ifndef BFC_TARGET_S390X
#define BFC_TARGET_S390X 1
#endif /* BFC_TARGET_S390X */
#ifndef BFC_TARGET_X86_64
#define BFC_TARGET_X86_64 1
#endif /* BFC_TARGET_X86_64 */
/* The target architecture - should be the same as the ELF e_machine value for
 * that architecture for consistency's sake. */
#ifndef BFC_DEFAULT_TARGET
#define BFC_DEFAULT_TARGET EM_X86_64
#endif /* BFC_DEFAULT_TARGET */

/* runs some preprocessor validation that the above settings are sane, and
 * defines some macros based on the configured default target architecture */
#define BFC_PREPROC_POST_CONFIG
#include "post_config.h"
#undef BFC_PREPROC_POST_CONFIG

#endif /* BFC_CONFIG_H */
