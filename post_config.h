/*
 * SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Preprocessor directives for compile-time validation that config.h is usable,
 * and setting of some macro constants based on it if so. */
#ifndef BFC_POST_CONFIG_H
#define BFC_POST_CONFIG_H 1
#ifndef BFC_PREPROC_POST_CONFIG
#error "post_config.h should only be #included by config.h"
#endif /* BFC_PREPROC_POST_CONFIG */

/* Validate that at least one target is enabled */
/* __BACKENDS__ each backend should be added to this check */
#if !( \
    BFC_TARGET_X86_64 || BFC_TARGET_ARM64 || BFC_TARGET_RISCV64 || \
    BFC_TARGET_S390X \
)
#error "No backends are enabled"
#endif

/* __BACKENDS__ add a block for the new backend
 * Each listed backend should first check if it's enabled, and result in a
 * compile-time error if it isn't. After that check, it should define the
 * BFC_DEFAULT_INTER and BFC_DEFAULT_ARCH_STR macros to appropriate values
 * for the target. */
#if BFC_DEFAULT_TARGET == EM_X86_64
#if !BFC_TARGET_X86_64
#error "BFC_DEFAULT_TARGET is EM_X86_64, but BFC_TARGET_X86_64 is off."
#endif /* !BFC_TARGET_X86_64  */
#define BFC_DEFAULT_INTER X86_64_INTER
#define BFC_DEFAULT_ARCH_STR "x86_64"

#elif BFC_DEFAULT_TARGET == EM_AARCH64
#if !BFC_TARGET_ARM64
#error "BFC_DEFAULT_TARGET is EM_AARCH64, but BFC_TARGET_ARM64 is off."
#endif /* !BFC_TARGET_ARM64 */
#define BFC_DEFAULT_INTER ARM64_INTER
#define BFC_DEFAULT_ARCH_STR "arm64"

#elif BFC_DEFAULT_TARGET == EM_RISCV
#if !BFC_TARGET_RISCV64
#error "BFC_DEFAULT_TARGET is EM_RISCV, but BFC_TARGET_RISCV64 is off."
#endif /* !BFC_TARGET_ARM64 */
#define BFC_DEFAULT_INTER RISCV64_INTER
#define BFC_DEFAULT_ARCH_STR "arm64"

#elif BFC_DEFAULT_TARGET == EM_S390
#if !BFC_TARGET_S390X
#error "BFC_DEFAULT_TARGET is EM_S390, but BFC_TARGET_S390X is off."
#endif /* !BFC_TARGET_S390X */
#define BFC_DEFAULT_INTER S390X_INTER
#define BFC_DEFAULT_ARCH_STR "s390x"

#else
#error "BFC_DEFAULT_TARGET is not set to a recognized value."
#endif /* BFC_DEFAULT_TARGET */

#if BFC_GNU_ARGS && !defined _GNU_SOURCE
#error "BFC_GNU_ARGS requires _GNU_SOURCE"
#endif

#endif /* BFC_POST_CONFIG_H */
