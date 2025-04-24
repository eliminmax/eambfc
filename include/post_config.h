/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Preprocessor directives for compile-time validation that config.h is usable,
 * and setting of some macro constants based on it if so, as well as inferring
 * the default backend to use based on the target architecture if the
 * BFC_DEFAULT_TARGET macro isn't already set. */

/* internal */

/* __BACKENDS__ add a backend ID with a unique, nonzero value here */
#define ARCH_ARM64 1
#define ARCH_RISCV64 2
#define ARCH_S390x 3
#define ARCH_X86_64 4

#ifndef BFC_POST_CONFIG_H
#define BFC_POST_CONFIG_H 1

#ifndef BFC_PREPROC_POST_CONFIG
#error "post_config.h should only be #included by config.h"
#endif /* BFC_PREPROC_POST_CONFIG */

/* __BACKENDS__ each backend should be added to this macro */
#define BFC_NUM_BACKENDS \
    BFC_TARGET_ARM64 + BFC_TARGET_RISCV64 + BFC_TARGET_S390X + BFC_TARGET_X86_64

/* Validate that at least one target is enabled */
#if BFC_NUM_BACKENDS == 0
#error "No backends are enabled"
#endif /* BFC_NUM_BACKENDS */

/* __BACKENDS__ number of backends should be incremented */
#if defined(BFC_TEST) && (BFC_NUM_BACKENDS != 4)
#error "unit testing is unsupported without all backends enabled"
#endif /* defined(BFC_TEST) && ... */

/* __BACKENDS__ add logic for the new backend
 *
 * Each backend block should check if the backend is enabled, and if the GCC
 * macro for the backend is defined, and use it if so
 * For a list of macros to use, see
 * https://github.com/cpredef/predef/blob/master/Architectures.md */

#ifndef BFC_DEFAULT_TARGET
#if BFC_TARGET_X86_64 && defined __x86_64__
#define BFC_DEFAULT_TARGET ARCH_X86_64
#elif BFC_TARGET_ARM64 && defined __aarch64__
#define BFC_DEFAULT_TARGET ARCH_ARM64
#elif BFC_TARGET_RISCV64 && defined __riscv
#define BFC_DEFAULT_TARGET ARCH_RISCV64
#elif BFC_TARGET_S390X && defined __s390x__
#define BFC_DEFAULT_TARGET ARCH_RISCV64

/* __BACKENDS__ Add fallback case here - these are used if none of the macros
 * to determine target architecture are defined, or the system's architecture is
 * disabled */

#elif BFC_TARGET_X86_64
#define BFC_DEFAULT_TARGET ARCH_X86_64
#elif BFC_TARGET_ARM64
#define BFC_DEFAULT_TARGET ARCH_ARM64
#elif BFC_TARGET_RISCV64
#define BFC_DEFAULT_TARGET ARCH_RISCV64
#else /* must be BFC_TARGET_S390X as established by previous check */
#define BFC_DEFAULT_TARGET ARCH_RISCV64
#endif /* BFC_DEFAULT_TARGET */
#endif /* BFC_DEFAULT_TARGET */

/* __BACKENDS__ add a block for the new backend
 * Each listed backend should first check if it's enabled, and result in a
 * compile-time error if it isn't. After that check, it should define the
 * BFC_DEFAULT_INTER and BFC_DEFAULT_ARCH_STR macros to appropriate values
 * for the target. */
#if BFC_DEFAULT_TARGET == ARCH_X86_64
#if !BFC_TARGET_X86_64
#error "BFC_DEFAULT_TARGET is ARCH_X86_64, but BFC_TARGET_X86_64 is off."
#endif /* !BFC_TARGET_X86_64  */
#define BFC_DEFAULT_INTER X86_64_INTER
#define BFC_DEFAULT_ARCH_STR "x86_64"

#elif BFC_DEFAULT_TARGET == ARCH_ARM64
#if !BFC_TARGET_ARM64
#error "BFC_DEFAULT_TARGET is ARCH_ARM64, but BFC_TARGET_ARM64 is off."
#endif /* !BFC_TARGET_ARM64 */
#define BFC_DEFAULT_INTER ARM64_INTER
#define BFC_DEFAULT_ARCH_STR "arm64"

#elif BFC_DEFAULT_TARGET == ARCH_RISCV64
#if !BFC_TARGET_RISCV64
#error "BFC_DEFAULT_TARGET is ARCH_RISCV64, but BFC_TARGET_RISCV64 is off."
#endif /* !BFC_TARGET_RISCV64 */
#define BFC_DEFAULT_INTER RISCV64_INTER
#define BFC_DEFAULT_ARCH_STR "riscv64"

#elif BFC_DEFAULT_TARGET == ARCH_S390X
#if !BFC_TARGET_S390X
#error "BFC_DEFAULT_TARGET is ARCH_S390X, but BFC_TARGET_S390X is off."
#endif /* !BFC_TARGET_S390X */
#define BFC_DEFAULT_INTER S390X_INTER
#define BFC_DEFAULT_ARCH_STR "s390x"

#else
#error "BFC_DEFAULT_TARGET is not set to a recognized value."
#endif /* BFC_DEFAULT_TARGET */

#if BFC_LONGOPTS && !defined _GNU_SOURCE
#error "BFC_LONGOPTS requires _GNU_SOURCE"
#endif /* BFC_LONGOPTS && !defined _GNU_SOURCE */
#if BFC_LONGOPTS && defined BFC_NOEXTENSIONS
#error "BFC_LONGOPTS cannot coexist with BFC_NOEXTENSIONS"
#endif

#define BFC_CHUNK_MASK (BFC_CHUNK_SIZE - 1)

#endif /* BFC_POST_CONFIG_H */
