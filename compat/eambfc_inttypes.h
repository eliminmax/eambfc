/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * a wrapper providing fallback uint64_t and int64_t typedefs, as they are not
 * guaranteed to on all supported systems. Also includes a 40-line rant about
 * standards and compatibility, for anyone who cares to see that. */

/* This header exists to handle an infuriating edge case that I need to rant
 * about, because I can't understand why POSIX does what it does here.
 *
 * POSIX requires that appropriate types exist for int8_t, int16_t, int32_t,
 * and their uintN_t equivalents, and that the PRI and SCN macros are as well.
 *
 * You know what it does not require?
 * Definitions for int64_t and uint64_t.
 *
 * You know what EAMBFC needs a lot of?
 * Fixed-size 64-bit integers, both signed and unsigned.
 *
 * C99 requires long long int and unsigned long long int to be 64-bits or more,
 * so if a platform decides to use 128-bit long long types, for whatever reason,
 * that's allowed. That makes sense for the approach C takes to things, and it's
 * an approach that I respect.
 *
 * POSIX, on the other hand, requires that fixed-size 8, 16, and 32-bit signed
 * and unsigned integer types exist. Why not require the same for 64-bit types?
 * Seriously!
 * Because EAMBFC, a personal hobby project I torture myself with for some
 * reason, requires that the eambfc code compiles and works on ANY system that
 * complies with the 2008 version of the POSIX.1 standard, I can't assume that
 * the types I need exist.
 *
 * POSIX.1-2008's C standard interface is an extension to the C99 standard, and
 * the full C99 standard, including long long int and unsigned long long int,
 * will be present.
 * So there's a type that can hold any value that int64_t can, and a type that
 * can hold any value that uint64_t can. It might not be the same range, but
 * it can be used to cheat at this. If I'm very careful, which I need to be, as
 * it's C I'm ranting about here, I can cheat. I can typedef it myself, and
 * define the macros I use myself, and pretend that a long long that's more than
 * 64 bits is actually only 64 bits.
 * 
 * It is fragile, and not desirable, but I can do it, as a fall-back if the
 * sane approach that *should* work (i.e. just #include inttypes or stdint)
 * fails.
 *
 * Phew. That rant took much longer to write than the rest of this file. */

#ifndef EAMBFC_INTTYPES_H
#define EAMBFC_INTTYPES_H 1
/* inttypes.h #includes <stdint.h> and provides printf and scanf macros. */
/* C99 */
#include <inttypes.h>

#ifndef INT64_MAX /* if not defined, int64_t is not supported. */
#define INT64_MAX 9223372036854775807LL
typedef long long int int64_t
#endif /* INT64_MAX */

#ifndef UINT64_MAX /* if not defined, uint64_t is not supported. */
#define UINT64_MAX 18446744073709551615ULL
typedef unsigned long long int uint64_t
#define PRIx64 "%llu"
#define SCNx64 "%llu"
#endif /* UINT64_MAX */

#ifdef INT_TORTURE_TEST
/* __int128 is a type supported by gcc on some platforms. Use it to make sure
 * that the logic does not break for >64-bit long long types */
#define int64_t __int128
#define uint64_t unsigned __int128
#endif /* INT_TORTURE_TEST */
#endif /* EAMBFC_INTTYPES_H */
