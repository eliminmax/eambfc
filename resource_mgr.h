/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Provides an interface to a semi-opaque "Resource Manager" which wraps around
 * `malloc`, `realloc`, `free`, `open`, and `close`, and tries to properly clean
 * up in case of any sort of failure.
 *
 * Internal calls to malloc or realloc that fail are treated as fatal errors,
 * but internal calls to open are not, as if memory allocation fails, then it's
 * assumed that any future failures will also fail, and the program can't
 * proceed, but if a file fails to open, it could be a permissions issue of some
 * kind, and if multiple files are to be compiled, others can still be
 * attempted.
 *
 * The "Resource Manager" registers an internal function with atexit to close
 * any registered file descriptors and free any registered allocations, though
 * that's intended as a fallback mechanism to be able to cleanly exit in case of
 * unexpected state, so allocations and file descriptors shouldn't be left open
 * just because that happens. */

#ifndef BFC_RESOURCE_MGR_H
#define BFC_RESOURCE_MGR_H 1
/* internal */
#include "types.h" /* mode_t, size_t */

/* MUST BE CALLED to register the internal cleanup function with atexit.
 *
 * Fatal Error Calls:
 * - if atexit failed to register Resource Manager cleanup function, calls
 *   internal_err with error code `MGR_ATEXIT_FAILED` */
void register_mgr(void);

/* mgr_malloc has the same semantics as malloc, but registers allocations that
 * it has returned, and any still registered will be freed when eambfc exits.
 *
 * Any allocation done with mgr_malloc must be 'realloc'ed and 'free'd with the
 * mgr_realloc and mgr_free functions, to ensure that they are properly tracked
 * and unregistered by the Resource Manager, otherwise it may try to free them
 * again, resulting in Double Frees or other Undefined Behavior, or run out of
 * space to register new allocations.
 *
 * Fatal Error Calls:
 *  - if called with 64 allocations already registered, calls internal_err with
 *    error code `TOO_MANY_ALLOCS`
 *  - if internal call to malloc fails, calls alloc_err
 *
 * Because it calls alloc_err on failure, it never returns NULL. */
void *mgr_malloc(size_t size);

/* mgr_realloc has the same semantics as realloc, assuming it succeeds, but it
 * exits on failure. Because of that, `p = mgr_realloc(p, size);` is not a bug.
 *
 * Fatal Error Calls:
 *  - if ptr is not from a registered allocation, calls internal_err with error
 *    code `MGR_REALLOC_UNKNOWN`
 *  - if internal call to realloc fails, frees ptr then calls alloc_err
 *
 * Because it calls alloc_err on failure, it never returns NULL. */
void *mgr_realloc(void *ptr, size_t size);

/* mgr_free has the same semantics as free, but unregisters the tracked
 * allocation.
 *
 * Fatal Error Calls:
 * - if ptr is not from a registered allocation, it calls internal_err with
 *   error code `MGR_FREE_UNKNOWN` */
void mgr_free(void *ptr);

/* mgr_open_m and mgr_open have the same semantics as a call to open with and
 * without a mode specified respectively. Both register the file descriptors
 * they return, and any still registered will be closed when eambfc exits.
 *
 * Any file descriptors opened with mgr_open or mgr_open_m must be closed with
 * mgr_close, to ensure that they are properly unregistered by the Resource
 * Manager, otherwise it may try to close them again, or run out of space to
 * register new file descriptors.
 *
 * If internal call to open returns -1, they don't register the file descriptor.
 *
 * Fatal Error Calls:
 *  - if called with 16 file descriptors already registered, call internal_err
 *    with error code `TOO_MANY_OPENS`. */
int mgr_open_m(const char *pathname, int flags, mode_t mode);
int mgr_open(const char *pathname, int flags);

/* mgr_close has the same semantics as close, but unregisters the tracked file
 * descriptor.
 *
 * Fatal Error Calls:
 * - if fd is not a registered file descriptor, it calls internal_err with
 *   error code `MGR_CLOSE_UNKNOWN` */
int mgr_close(int fd);
#endif /* BFC_RESOURCE_MGR_H */
