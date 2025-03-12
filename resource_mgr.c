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
#include "types.h"

#define MAX_ALLOCS 64
#define MAX_FDS 16

/* with the maximum numbers of entries as low as they are, no fancy data
 * structure needed. A struct is used just to keep all of the Resource Manager's
 * internal state in one place. */
static struct resource_tracker {
    void *allocs[MAX_ALLOCS];
    int fds[MAX_FDS];
    ifast_8 next_a;
    ifast_8 next_f;
} resources;

static ifast_8 alloc_index(const void *ptr) {
    /* work backwards, as more recent allocs are more likely to be used */
    for (ifast_8 i = resources.next_a - 1; i >= 0; --i) {
        if (resources.allocs[i] == ptr) return i;
    }
    return -1;
}

nonnull_ret void *mgr_malloc(size_t size) {
    if (resources.next_a > MAX_ALLOCS - 1) {
        internal_err(
            BF_ICE_TOO_MANY_ALLOCS,
            "Allocated too many times for resource_mgr to track."
        );
        /* will never return, as internal_err calls exit(EXIT_FAILURE) */
    }

    void *result = malloc(size);
    if (result == NULL) {
        /* don't skip over this array index */
        resources.next_a--;
        alloc_err();
    } else {
        resources.allocs[resources.next_a++] = result;
    }
    return result;
}

void mgr_free(void *ptr) {
    ifast_8 index = alloc_index(ptr);
    if (index == -1) {
        internal_err(
            BF_ICE_MGR_FREE_UNKNOWN,
            "mgr_free called with an unregistered *ptr value"
        );
        /* will never return, as internal_err calls exit(EXIT_FAILURE) */
        return;
    }
    free(ptr);
    size_t to_move = (resources.next_a - index) * sizeof(void *);
    memmove(
        &(resources.allocs[index]), &(resources.allocs[index + 1]), to_move
    );
    resources.next_a--;
}

nonnull_args nonnull_ret void *mgr_realloc(void *ptr, size_t size) {
    ifast_8 index = alloc_index(ptr);
    if (index == -1) {
        /* will never return, as internal_err calls exit(EXIT_FAILURE) */
        internal_err(
            BF_ICE_MGR_REALLOC_UNKNOWN,
            "mgr_realloc called with an unregistered *ptr value"
        );
    }
    void *new_ptr = realloc(ptr, size);
    if (new_ptr == NULL) {
        free(resources.allocs[index]);
        alloc_err();
    }
    resources.allocs[index] = new_ptr;
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
        /* will never return, as internal_err calls exit(EXIT_FAILURE) */
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
    ifast_8 index = -1;
    /* work backwards - more likely to close more recently-opened files */
    for (ifast_8 i = resources.next_f - 1; i >= 0; i--) {
        if (resources.fds[i] == fd) {
            index = i;
            break;
        }
    }
    if (index == -1) {
        internal_err(
            BF_ICE_MGR_CLOSE_UNKNOWN,
            "mgr_close called with an unregistered fd value"
        );
        /* will never return, as internal_err calls exit(EXIT_FAILURE) */
    } else {
        /* remove fd from resources */
        size_t to_move = ((resources.next_f--) - index) * sizeof(int);
        memmove(&(resources.fds[index]), &(resources.fds[index + 1]), to_move);
    }
    return close(fd);
}

void cleanup(void) {
    while (--resources.next_a > -1) free(resources.allocs[resources.next_a]);
    while (--resources.next_f > -1) close(resources.fds[resources.next_f]);
}

void register_mgr(void) {
    static bool registered = false;
    /* if already registered, do nothing. */
    if (registered) return;
    /* atexit returns 0 on success, and a nonzero value otherwise. */
    if (atexit(cleanup)) {
        internal_err(
            BF_ERR_MGR_ATEXIT_FAILED,
            "Failed to register cleanup function with atexit"
        );
    } else {
        registered = true;
    }
}
