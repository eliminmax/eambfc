/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Macros which optionally resolve to attributes if supported, to enable better
 * static analysis and/or optimization */

#ifndef BFC_ATTRIBUTES
#define BFC_ATTRIBUTES 1
#define noreturn
#define nonnull_args
#define nonnull_arg(...)
#define nonnull_ret
#define const_fn
#define malloc_like

#if __STDC_VERSION__ == 202311L
#undef noreturn
#define noreturn [[noreturn]]

#elif __STDC_VERSION__ == 201710L || __STDC_VERSION__ == 201112L
#undef noreturn
#define noreturn _Noreturn

#elif defined(__GNUC__)
#undef noreturn
#define noreturn __attribute__((noreturn))

#endif /* STDC_VERSION */

#ifdef __GNUC__

#undef nonnull_args
#define nonnull_args __attribute__((nonnull))

#undef nonnull_arg
#define nonnull_arg(...) __attribute__((nonnull(__VA_ARGS__)))

#undef nonnull_ret
#define nonnull_ret __attribute__((returns_nonnull))

#undef const_fn
#define const_fn __attribute__((const))

#undef malloc_like
#define malloc_like __attribute__((malloc))

#endif /* __GNUC__ */
#endif /* BFC_NOATTRIBUTES */
