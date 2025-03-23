/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * provides wrappers around various functions that need cleanup at the end of
 * the process, and calls the cleanup functions as needed if the end was reached
 * improperly */

/* C99 */
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <fcntl.h>
#include <unistd.h>
/* internal */
#include "attributes.h"
#include "err.h"
#include "resource_mgr.h"
#include "types.h"

/* with the maximum numbers of entries as low as they are, no fancy data
 * structure needed. A struct is used just to keep all of the Resource Manager's
 * internal state in one place. */
static struct resource_tracker {
    uintptr_t allocs[MAX_ALLOCS];
    int fds[MAX_FDS];
    ifast_8 next_a;
    ifast_8 next_f;
} resources;

/* return the index of the allocation, or -1 if it's not a managed allocation */
static ifast_8 alloc_index(uintptr_t ptr) {
    /* work backwards, as more recent allocs are more likely to be used */
    for (ifast_8 i = resources.next_a - 1; i >= 0; --i) {
        if (resources.allocs[i] == ptr) return i;
    }
    return -1;
}

malloc_like nonnull_ret void *mgr_malloc(size_t size) {
    if (resources.next_a > MAX_ALLOCS - 1) {
        internal_err(
            BF_ICE_TOO_MANY_ALLOCS,
            "Allocated too many times for resource_mgr to track."
        );
    }

    void *result = malloc(size);
    if (result == NULL) {
        /* don't skip over this array index */
        resources.next_a--;
        alloc_err();
    }
    resources.allocs[resources.next_a++] = (uintptr_t)result;
    return result;
}

void mgr_free(void *ptr) {
    ifast_8 index = alloc_index((uintptr_t)ptr);
    free(ptr);
    if ((index) != -1) {
        memmove(
            &(resources.allocs[index]),
            &(resources.allocs[index + 1]),
            (resources.next_a - index) * sizeof(void *)
        );
        resources.next_a--;
    }
}

nonnull_args nonnull_ret void *mgr_realloc(void *ptr, size_t size) {
    ifast_8 index = alloc_index((uintptr_t)ptr);
    void *new_ptr = realloc(ptr, size);
    if (new_ptr == NULL) {
        /* if it's a managed pointer, it will be freed when alloc_err calls
         * mgr_cleanup. Otherwise, free it here. */
        if (index != -1) free(ptr);
        alloc_err();
    }
    if (index != -1) resources.allocs[index] = (uintptr_t)new_ptr;
    return new_ptr;
}

static nonnull_args int mgr_open_handler(
    const char *pathname, int flags, mode_t mode, bool with_mode
) {
    if (resources.next_f > MAX_FDS - 1) {
        internal_err(
            BF_ICE_TOO_MANY_OPENS,
            "Opened too many files for resource_mgr to track."
        );
        return -1;
    }
    int result;
    if (with_mode) {
        result = open(pathname, flags, mode);
    } else {
        result = open(pathname, flags);
    }

    if (result != -1) resources.fds[resources.next_f++] = result;
    return result;
}

/* same semantics as open with a specified mode */
nonnull_args int mgr_open_m(const char *pathname, int flags, mode_t mode) {
    return mgr_open_handler(pathname, flags, mode, true);
}

/* same semantics as open without a specified mode */
nonnull_args int mgr_open(const char *pathname, int flags) {
    return mgr_open_handler(pathname, flags, 0, false);
}

int mgr_close(int fd) {
    /* work backwards - more likely to close more recently-opened files */
    for (ifast_8 i = resources.next_f - 1; i >= 0; i--) {
        if (resources.fds[i] == fd) {
            memmove(
                &(resources.fds[i]),
                &(resources.fds[i + 1]),
                ((resources.next_f--) - i) * sizeof(int)
            );
            break;
        }
    }
    /* remove fd from resources */
    return close(fd);
}

void mgr_cleanup(void) {
    while (--resources.next_a > -1) {
        free((void *)resources.allocs[resources.next_a]);
    }
    while (--resources.next_f > -1) close(resources.fds[resources.next_f]);
}
