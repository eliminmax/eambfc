/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * provides wrappers around various functions that need cleanup at the end of
 * the process, and calls the cleanup functions as needed if the end was reached
 * improperly */
/* C99 */
#include <stdlib.h> /* {m,re}alloc, free, atexit */
#include <string.h> /* memmove */
/* POSIX */
#include <unistd.h> /* close */
#include <fcntl.h> /* open */
/* internal */
#include "types.h" /* ifast_8, bool, mode_t, size_t */
#include "err.h" /* internal_err, alloc_err */

#define MAX_ALLOCS 64
#define MAX_FDS 16

/* with the maximum numbers of entries as low as they are, no fancy data
 * structure needed. A struct is used just to keep all of the Resource Manager's
 * internal state in one place. */
static struct resource_tracker {
    void *allocs[MAX_ALLOCS];
    int fds[MAX_FDS];
    /* index variables are for the NEXT entry in the array. */
    ifast_8 alloc_i;
    ifast_8 fd_i;
} resources;

static ifast_8 alloc_index(void *ptr) {
    for (ifast_8 i = 0; i <= resources.alloc_i; i++) {
        if (resources.allocs[i] == ptr) return i;
    }
    return -1;
}

void *mgr_malloc(size_t size) {
    if (++resources.alloc_i > MAX_ALLOCS) {
        internal_err(
            "TOO_MANY_ALLOCS",
            "Allocated too many times for resource_mgr to track."
        );
        /* will never return, as internal_err calls exit(EXIT_FAILURE) */
        return NULL;
    }

    void* result = malloc(size);
    if (result == NULL) {
        /* don't skip over this array index */
        resources.alloc_i--;
        alloc_err();
    } else {
        resources.allocs[resources.alloc_i] = result;
    }
    return result;
}

void mgr_free(void *ptr) {
    ifast_8 index = alloc_index(ptr);
    if (index == -1) {
        internal_err(
            "MGR_FREE_UNKNOWN",
            "mgr_free called with an unregistered *ptr value"
        );
        return;
    }
    size_t to_move = (index - resources.alloc_i) * sizeof(void*);
    memmove(
        &(resources.allocs[index]),
        &(resources.allocs[index + 1]),
        to_move
    );
}

void *mgr_realloc(void *ptr, size_t size) {
    ifast_8 index = alloc_index(ptr);
    if (index == -1) {
        internal_err(
            "MGR_REALLOC_UNKNOWN",
            "mgr_realloc called with an unregistered *ptr value"
        );
        return NULL;
    }
    void *new_ptr = realloc(ptr, size);
    if (new_ptr == NULL) {
        free(resources.allocs[index]);
        alloc_err();
        return NULL;
    }
    resources.allocs[index] = new_ptr;
    return new_ptr;
}

static int mgr_open_handler(
    const char *pathname, int flags, mode_t mode, bool with_mode
) {
    if (++resources.fd_i > MAX_FDS) {
        internal_err(
            "TOO_MANY_OPENS",
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
    if (result == -1) {
        /* don't skip over this array index */
        resources.fd_i--;
    } else {
        resources.fds[resources.fd_i] = result;
    }
    return result;
}

/* same semantics as open with a specified mode */
int mgr_open_m(const char *pathname, int flags, mode_t mode) {
    return mgr_open_handler(pathname, flags, mode, true);
}

/* same semantics as open without a specified mode */
int mgr_open(const char *pathname, int flags) {
    return mgr_open_handler(pathname, flags, 0, false);
}

int mgr_close(int fd) {
    ifast_8 index = -1;
    for (ifast_8 i = 0; i <= resources.fd_i; i++) {
        if (resources.fds[i] == fd) {
            index = i;
            break;
        }
    }
    if (index == -1) {
        internal_err(
            "MGR_CLOSE_UNKNOWN",
            "mgr_close called with an unregistered fd value"
        );
        /* will never return, as internal_err calls exit(EXIT_FAILURE) */
        return -1;
    }
    /* remove fd from resources */
    size_t to_move = (index - resources.fd_i) * sizeof(int);
    memmove(&(resources.fds[index]), &(resources.fds[index + 1]), to_move);
    resources.fd_i--;
    return close(fd);
}

void cleanup(void) {
    while (--resources.alloc_i > -1) free(resources.allocs[resources.alloc_i]);
    while (--resources.fd_i > -1) close(resources.fds[resources.fd_i]);
}

void register_mgr(void) {
    static bool registered = false;
    /* if already registered, do nothing. */
    if (registered) return;
    /* atexit returns 0 on success, and a nonzero value otherwise. */
    if (atexit(cleanup)) {
        internal_err(
            "MGR_ATEXIT_FAILED",
            "Failed to register cleanup function with atexit"
        );
    } else {
        registered = true;
    }
}
