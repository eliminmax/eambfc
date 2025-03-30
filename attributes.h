/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Macros which optionally resolve to attributes if supported, to enable better
 * static analysis and/or optimization */
#ifndef BFC_ATTRIBUTES
#define BFC_ATTRIBUTES 1

#if defined __GNUC__ && defined __has_attribute
#define HAS_GCC_ATTR(attr) __has_attribute(attr)
#else /* defined __GNUC__ && defined __has_attribute */
#define HAS_GCC_ATTR(attr) 0
#endif /* defined __GNUC__ && defined __has_attribute */

#define noreturn
#define nonnull_args
#define nonnull_arg(...)
#define nonnull_ret
#define const_fn
#define malloc_like
#define must_use

#if __STDC_VERSION__ >= 202311L
#undef noreturn
#define noreturn [[noreturn]]

#elif __STDC_VERSION__ == 201710L || __STDC_VERSION__ == 201112L
#undef noreturn
#include <stdnoreturn.h>

#elif HAS_GCC_ATTR(__noreturn__)
#undef noreturn
#define noreturn __attribute__((__noreturn__))
#endif /* STDC_VERSION */

#if HAS_GCC_ATTR(__const__)
#undef const_fn
#define const_fn __attribute__((__const__))
#endif /* HAS_GCC_ATTR(__const__) */

#if HAS_GCC_ATTR(__malloc__)
#undef malloc_like
#define malloc_like __attribute__((__malloc__))
#endif /* HAS_GCC_ATTR(__malloc__) */

#if HAS_GCC_ATTR(__nonnull__)
#undef nonnull_args
#define nonnull_args __attribute__((__nonnull__))
#undef nonnull_arg
#define nonnull_arg(...) __attribute__((nonnull(__VA_ARGS__)))
#endif /* HAS_GCC_ATTR(__nonnull__) */

#if HAS_GCC_ATTR(__returns_nonnull__)
#undef nonnull_ret
#define nonnull_ret __attribute__((__returns_nonnull__))
#endif /* HAS_GCC_ATTR(__returns_nonnull__) */

#if HAS_GCC_ATTR(__warn_unused_result__)
#undef must_use
#define must_use __attribute__((__warn_unused_result__))
#endif /* HAS_GCC_ATTR(__warn_unused_result__) */

#endif /* BFC_NOATTRIBUTES */
