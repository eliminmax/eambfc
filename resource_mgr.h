/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Declares an interface to a semi-opaque "Resource Manager" which wraps around
 * `malloc`, `realloc`, `free` from the C standard library, as well as the POSIX
 * `open` and `close` functions, and provides a function free all tracked
 * allocations and close all open fds, to be called before exiting when fatal
 * errors occur.
 *
 * Internal calls to malloc or realloc that fail are treated as fatal errors,
 * but internal calls to open are not, as if memory allocation fails, then it's
 * assumed that any future failures will also fail, and the program can't
 * proceed, but if a file fails to open, it could be a permissions issue of some
 * kind, and if multiple files are to be compiled, others can still be
 * attempted. */

#ifndef BFC_RESOURCE_MGR_H
#define BFC_RESOURCE_MGR_H 1
/* internal */
#include "attributes.h"
#include "types.h"

#define MAX_ALLOCS 64
#define MAX_FDS 16

/* Cleans up any active managed allocations or file descriptors.
 * Should be called before any abnormal exits */
void mgr_cleanup(void);

/* `mgr_free` has the same semantics as `free`, but if `ptr` is a managed
 * allocation, it also removes it from the resource tracker to make room for
 * further managed allocations, and  avoid a double-free if a fatal error
 * occurs. */
void mgr_free(void *ptr);

/* mgr_malloc has the same semantics as malloc, but registers allocations that
 * it has returned, and any still registered will be freed when eambfc exits.
 *
 * Any allocation done with mgr_malloc must be 'realloc'ed and 'free'd with the
 * mgr_realloc and mgr_free functions, to ensure that they are properly tracked
 * and unregistered by the Resource Manager, otherwise it may try to free them
 * again, resulting in double frees or other undefined behavior, or it could run
 * out of space to register new allocations.
 *
 * Fatal Error Calls:
 *  - if called with `MAX_ALLOCS` allocations already registered, calls
 *    `internal_err(BF_ICE_TOO_MANY_ALLOCS, ...)`
 *  - if internal call to `malloc` fails, calls `alloc_err`
 *
 * Because it calls `alloc_err` on failure, it never returns `NULL`. */
must_use malloc_like nonnull_ret void *mgr_malloc(size_t size);

/* mgr_realloc has the same semantics as a realloc call where the reallocation
 * succeeded, but on failure, it calls alloc_err. Because of that, unlike
 * `p = realloc(p, sz)`, `p = mgr_realloc(p, sz)` is perfectly safe.
 *
 * If ptr is a managed allocation, it updates the tracked pointer, but if it
 * does not, it just provides the checked reallocation.
 *
 * Because it calls `alloc_err` on failure, it never returns `NULL`. */
must_use nonnull_args nonnull_ret void *mgr_realloc(void *ptr, size_t size);

/* mgr_open_m and mgr_open have the same semantics as a call to open with and
 * without a mode specified respectively. Both register the file descriptors
 * they return, and any still registered will be closed when eambfc exits.
 *
 * Any file descriptors opened with mgr_open or mgr_open_m must be closed with
 * mgr_close, to ensure that they are properly unregistered by the Resource
 * Manager, otherwise it may try to close them again, or run out of space to
 * register new file descriptors.
 *
 * Fatal Error Calls:
 *  - if called with MAX_FDS file descriptors already registered, call
 *    internal_err with error code ICE:MgrTooManyOpens. */
must_use nonnull_args int mgr_open_m(
    const char *pathname, int flags, mode_t mode
);

/* repeat docstring for clangd to use for both functions */

/* mgr_open_m and mgr_open have the same semantics as a call to open with and
 * without a mode specified respectively. Both register the file descriptors
 * they return, and any still registered will be closed when eambfc exits.
 *
 * Any file descriptors opened with mgr_open or mgr_open_m must be closed with
 * mgr_close, to ensure that they are properly unregistered by the Resource
 * Manager, otherwise it may try to close them again, or run out of space to
 * register new file descriptors.
 *
 * Fatal Error Calls:
 *  - if called with MAX_FDS file descriptors already registered, call
 *    internal_err with error code ICE:MgrTooManyOpens. */
must_use nonnull_args int mgr_open(const char *pathname, int flags);

/* mgr_close calls close, but if fd was opened by mgr_open or mgr_open_m, it
 * also unregisters it from the resource manager, so that it doesn't try to
 * automatically close it on error. */
int mgr_close(int fd);
#endif /* BFC_RESOURCE_MGR_H */
