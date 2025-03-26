/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#ifndef TEST_UTILS_H
#define TEST_UTILS_H 1
/* C99 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <unistd.h>
/* internal */
#include "../attributes.h"
#include "../config.h"
#include "../types.h"

#ifdef BFC_TEST_UTILS_C
#define inline_impl extern inline
#else /* BFC_TEST_UTILS_C */
#define inline_impl inline
#endif /* BFC_TEST_UTILS_C */

nonnull_args inline_impl bool sb_eq(const sized_buf *a, const sized_buf *b) {
    return a->sz == b->sz && memcmp(a->buf, b->buf, a->sz) == 0;
}

/* malloc, aborting on failure*/
nonnull_ret malloc_like inline_impl void *checked_malloc(size_t sz) {
    void *ret = malloc(sz);
    if (ret == NULL) {
        perror("Failed allocation");
        abort();
    }
    return ret;
}

/* realloc, freeing ptr and aborting on failure */
nonnull_ret inline_impl void *checked_realloc(void *ptr, size_t sz) {
    void *ret = realloc(ptr, sz);
    if (ret == NULL) {
        perror("Failed allocation");
        free(ptr);
        abort();
    }
    return ret;
}

/* read up to `BFC_CHUNK_SIZE` from `fd` and append the bytes that were read to
 * `dst`, reallocating as needed. */
nonnull_args size_t read_chunk(sized_buf *dst, int fd);

/* Return the smallest multiple of `BFC_CHUNK_SIZE` that's at least `sz`.
 *
 * If sz is already a multiple of `BFC_CHUNK_SIZE`, or if there are no multiples
 * of `BFC_CHUNK_SIZE` greater than `sz` but less than `SIZE_MAX`, `sz` is
 * returned unchanged. */
inline_impl const_fn size_t mempad(size_t sz) {
    size_t alloc_sz = (sz & (BFC_CHUNK_SIZE - 1)) ?
                          (sz & ~(BFC_CHUNK_SIZE - 1)) + BFC_CHUNK_SIZE :
                          sz;
    /* on overflow, fall back to the original sz value */
    if (alloc_sz < sz) alloc_sz = sz;
    return alloc_sz;
}

/* this function takes care of the fork/exec/wait boilerplate, aborting if the
 * fork fails or the child stops abnormally, otherwise returning the child's
 * exit status.
 *
 * `args` must be ended with a NULL pointer.
 *
 * Safer and easier to use than `system`, if more than one arg is needed or args
 * could cause shell expansion. */
nonnull_args int subprocess(const char *args[]);

/* Execute a command with the provided args, returning the lowest 8 bits of its
 * exit code, or -1 if it exited abnormally.
 *
 * `args` is an array of arguments, terminated by NULL,
 * `args[0]` is the executable to run, and `args` (without the NULL) is the
 * spawned process's `argv`.
 * `out` and `err` point to sized_bufs that the program invocation's `stdout`
 * and `stderr` can be captured to.
 *
 * If not needed, one or both of `out` and `err` can be set to NULL, in which
 * case their respective streams are not redirected. */
nonnull_arg(1) int run_capturing(
    const char *args[], sized_buf *out, sized_buf *err
);

/* convenience macro to fprintf to stderr */
#define EPRINTF(fmt, ...) fprintf(stderr, fmt, __VA_ARGS__)

#define STRINGIFY(x) #x
/* stringify macros like `__LINE__` */
#define METASTRINGIFY(x) STRINGIFY(x)

#define PREREQ_FAIL(s) \
    __FILE__ ":" METASTRINGIFY(__LINE__) ": prerequisite `" s \
                                         "` evaluated to false"
/* if `expect`  evaluates to 0, call `perror` with the current location followed
 * by a stringified version of expect */
#define CHECKED(expect) \
    do { \
        if (!(expect)) { \
            perror(PREREQ_FAIL(#expect)); \
            abort(); \
        } \
    } while (false)

/* an alternative to the `CHECKED` macro which is safe to call between fork and
 * exec, or after exec fails */
#define POST_FORK_CHECKED(expect) \
    do { \
        if (!(expect)) { \
            write( \
                STDOUT_FILENO, \
                PREREQ_FAIL(#expect) " between fork() and successful exec().", \
                strlen(PREREQ_FAIL(#expect)) + 38 \
            ); \
            abort(); \
        } \
    } while (false)

#undef inline_impl
#endif /* TEST_UTILS_H */
