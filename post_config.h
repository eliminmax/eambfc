/*
 * SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Preprocessor directives for compile-time validation that config.h is usable,
 * and setting of some macro constants based on it if so. */
#ifndef EAMBFC_POST_CONFIG_H
#define EAMBFC_POST_CONFIG_H 1
#ifndef EAMBFC_PREPROC_POST_CONFIG
#error "post_config.h should only be #included by config.h"
#endif /* EAMBFC_PREPROC_POST_CONFIG */

/* Validate that at least one target is enabled */
/* __BACKENDS__ each backend should be added to this check */
#if !(EAMBFC_TARGET_X86_64 || EAMBFC_TARGET_ARM64 || EAMBFC_TARGET_S390X)
#error "No backends are enabled"
#endif

/* __BACKENDS__
 * Each listed backend should first check if it's enabled, and result in a
 * compile-time error if it isn't. After that check, it should define the
 * EAMBFC_DEFAULT_INTER and EAMBFC_DEFAULT_ARCH_STR macros to appropriate values
 * for the target. */
#if EAMBFC_DEFAULT_TARGET == EM_X86_64
#if !EAMBFC_TARGET_X86_64
#error "EAMBFC_DEFAULT_TARGET is EM_X86_64, but EAMBFC_TARGET_X86_64 is off."
#endif /* !EAMBFC_TARGET_X86_64  */
#define EAMBFC_DEFAULT_INTER X86_64_INTER
#define EAMBFC_DEFAULT_ARCH_STR "x86_64"

#elif EAMBFC_DEFAULT_TARGET == EM_AARCH64
#if !EAMBFC_TARGET_ARM64
#error "EAMBFC_DEFAULT_TARGET is EM_AARCH64, but EAMBFC_TARGET_ARM64 is off."
#endif /* EAMBFC_TARGET_ARM64 == 0 */
#define EAMBFC_DEFAULT_INTER ARM64_INTER
#define EAMBFC_DEFAULT_ARCH_STR "arm64"

#elif EAMBFC_DEFAULT_TARGET == EM_S390
#if !EAMBFC_TARGET_S390X
#error "EAMBFC_DEFAULT_TARGET is EM_S390, but EAMBFC_TARGET_S390X is off."
#endif /* EAMBFC_TARGET_S390X == 0 */
#define EAMBFC_DEFAULT_INTER S390X_INTER
#define EAMBFC_DEFAULT_ARCH_STR "s390x"

#else
#error "EAMBFC_DEFAULT_TARGET is not set to a recognized value."
#endif /* EAMBFC_DEFAULT_TARGET */

#endif /* EAMBFC_POST_CONFIG_H */
