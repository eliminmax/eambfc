/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Utility functions for use in test_driver.c, split out because it was getting
 * too unwieldy */

/* C99 */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <sys/wait.h>
#include <unistd.h>

/* internal */
#include "../types.h"
#define BFC_TEST_UTILS_C
#include "test_utils.h"

/* read up to `BFC_CHUNK_SIZE` from `fd` and append the bytes that were read to
 * `dst`, reallocating as needed. */
nonnull_args size_t read_chunk(sized_buf *dst, int fd) {
    char buf[BFC_CHUNK_SIZE];
    ssize_t ct;
    CHECKED((ct = read(fd, buf, BFC_CHUNK_SIZE)) >= 0);
    if (dst->sz > (SIZE_MAX - (size_t)ct)) {
        fprintf(
            stderr,
            "Appending %jd bytes to an object of %ju bytes would overflow\n",
            (intmax_t)ct,
            (uintmax_t)dst->sz
        );
        abort();
    }

    size_t needed_cap = dst->sz + ct;
    if (needed_cap > dst->capacity) {
        dst->buf = checked_realloc(dst->buf, mempad(needed_cap));
    }
    memcpy(dst->buf + dst->sz, buf, ct);
    dst->sz += ct;
    return ct;
}

/* try to execv(args[0], args), and print an error then abort on failure,
 * and casting args from a `const char *const []` to a `char *const *`, which
 * */
noreturn static void execv_const(const char *const args[]) {
    /* This cast is risky, but the POSIX standard explicitly requires that args
     * aren't modified, and clarifies that the use of `char *const[]` instead of
     * `const char *const[]` is for compatibility with existing code calling the
     * execv* variants of `exec`
     *
     * As long as the POSIX requirement is followed, this is fine. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
    execv(args[0], (char *const *)args);
#pragma GCC diagnostic pop
    /* Between fork and exec, only async-signal-safe functions are to be called
     * see the fork(3p) and signal-safety(7) man pages for details.
     *
     * Because exec failed if this is reached, only functions which are safe to
     * call between fork and exec should be called, so `fprintf` and `fputs` are
     * not safe to use here, but calling `write` with the stderr fd is. */
    write(STDERR_FILENO, "Failed to exec.\n", 16);
    abort();
}

nonnull_arg(1) int run_capturing(
    const char *args[], sized_buf *out, sized_buf *err
) {
    int chld_status;
    pid_t chld;
    int out_pipe[2];
    int err_pipe[2];
    if (out) CHECKED(pipe(out_pipe) == 0);
    if (err) CHECKED(pipe(err_pipe) == 0);
    CHECKED((chld = fork()) != -1);

    if (chld == 0) {
        if (out != NULL) {
            POST_FORK_CHECKED(dup2(out_pipe[1], STDOUT_FILENO) >= 0);
            POST_FORK_CHECKED(close(out_pipe[0]) == 0);
            POST_FORK_CHECKED(close(out_pipe[1]) == 0);
        }
        if (err != NULL) {
            POST_FORK_CHECKED(dup2(err_pipe[1], STDERR_FILENO) >= 0);
            POST_FORK_CHECKED(close(err_pipe[0]) == 0);
            POST_FORK_CHECKED(close(err_pipe[1]) == 0);
        }
        execv_const(args);
    }

    if (out != NULL) CHECKED(close(out_pipe[1]) == 0);
    if (err != NULL) CHECKED(close(err_pipe[1]) == 0);
    while ((out != NULL && read_chunk(out, out_pipe[0])) ||
           (err != NULL && read_chunk(err, err_pipe[0])));
    CHECKED(waitpid(chld, &chld_status, 0) != -1);
    if (out != NULL) CHECKED(close(out_pipe[0]) == 0);
    if (err != NULL) CHECKED(close(err_pipe[0]) == 0);
    if (!WIFEXITED(chld_status)) {
        if (WIFSTOPPED(chld_status)) kill(chld, SIGTERM);
        return -1;
    }
    return WEXITSTATUS(chld_status);
}

nonnull_args int subprocess(const char *args[]) {
    if (args[0] == NULL) {
        fputs("exec_args called with empty args.\n", stderr);
        abort();
    }
    pid_t chld;
    CHECKED((chld = fork()) != -1);
    if (chld == 0) execv_const(args);
    int chld_status;
    CHECKED(waitpid(chld, &chld_status, 0) != -1);
    CHECKED(WIFEXITED(chld_status));
    return WEXITSTATUS(chld_status);
}
