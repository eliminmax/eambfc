/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Runs tests of eambfc's command-line behavior, with no dependencies beyond a
 * POSIX-compliant libc, and no use of eambfc's internal functions */

/* C99 */
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* internal - only macros and typedefs */
#include "../attributes.h"
#include "../config.h"
#include "../types.h"
/* internal - A header which provides a macro to generate a string containing
 * the expected output of colortest using the C Preprocessor */
#include "colortest_output.h"
#include "test_utils.h"

/* pointer to a string containing the path to the EAMBFC executable.
 *
 * Set in the main function to the `EAMBFC` environment variable, falling back
 * to `"../eambfc"` if `getenv("EAMBFC")` returns `NULL`.
 *
 * Once set, it should be left unchanged through the duration of the program. */
static const char *EAMBFC;

/* convenience macro to prepend the eambfc executable as args[0] and append NULL
 * to arguments */
#define ARGS(...) \
    (const char *[]) { \
        EAMBFC, __VA_ARGS__, NULL \
    }

static const char *ARCHES[BFC_NUM_BACKENDS + 1];

enum arch_support_status {
    CAN_RUN = 0,
    CANT_RUN = 1,
    ARCH_DISABLED = 2,
    UNKNOWN_ARCH = 3,
    UNINIT = 3,
};

/* __BACKENDS__ add a uint: 2 for architecture, with name matching name of both
 * execfmt_support binary and eambfc "-a" parameter, and initialize with a value
 * of `UNINIT` */
/* information about supported architectures */
static struct {
    uint arm64  : 2;
    uint riscv64: 2;
    uint s390x  : 2;
    uint x86_64 : 2;
    bool init   : 1;
} supported_arches = {UNINIT, UNINIT, UNINIT, UNINIT, false};

static void load_arch_support(void) {
    int arch_i = 0;
#define SUPPORT_CHECK(ARCH_CFG, arch) \
    do { \
        if (ARCH_CFG) { \
            ARCHES[arch_i++] = #arch; \
            if (system("../tools/execfmt_support/" #arch) == 0) { \
                supported_arches.arch = CAN_RUN; \
            } else { \
                supported_arches.arch = CANT_RUN; \
            } \
        } else { \
            supported_arches.arch = ARCH_DISABLED; \
        } \
    } while (0)

    /* __BACKENDS__ add support check */
    SUPPORT_CHECK(BFC_TARGET_ARM64, arm64);
    SUPPORT_CHECK(BFC_TARGET_RISCV64, riscv64);
    SUPPORT_CHECK(BFC_TARGET_S390X, s390x);
    SUPPORT_CHECK(BFC_TARGET_X86_64, x86_64);
#undef SUPPORT_CHECK

    supported_arches.init = true;
}

static enum arch_support_status support_status(const char *arch) {
    if (!supported_arches.init) load_arch_support();
    /* number of architectures is small enough that a hash map isn't worth it */
#define ARCH_CHECK(arch_id) \
    if (strcmp(arch, #arch_id) == 0) return supported_arches.arch_id;

    /* __BACKENDS__ add arch check */
    ARCH_CHECK(arm64);
    ARCH_CHECK(riscv64);
    ARCH_CHECK(s390x);
    ARCH_CHECK(x86_64);
#undef ARCH_CHECK

    return UNKNOWN_ARCH;
}

typedef struct result_tracker {
    u8 skipped;
    u8 failed;
    u8 succeeded;
} result_tracker;

/* Information about binary tests. */
typedef struct bintest {
    /* the argv[0] value when running the binary */
    const char *test_bin;
    /* the expected output of the binary */
    const char *expected;
    /* the size of the expected output */
    size_t expected_sz;
} bintest;

static const bintest BINTESTS[] = {
    {"./colortest", COLORTEST_OUTPUT, COLORTEST_OUTPUT_LEN},
    {"./hello", "Hello, world!\n", 14},
    {"./loop", "!", 1},
    {"./null", "", 0},
    {"./wrap", "\xf0\x9f\xa7\x9f" /* utf8-encoded zombie emoji */, 4},
    {"./wrap2", "0000", 4},
    {"./dead_code", "", 0},
};

#define NBINTESTS 7

typedef enum test_outcome {
    TEST_FAILED = -1,
    TEST_SUCCEEDED = 0,
    TEST_SKIPPED = 1,
} test_outcome;

/* test a binary which takes no input, making sure it exists successfully after
 * writing the expected data to stdout */
static test_outcome bin_test(ifast_8 bt, const char *restrict arch, bool opt) {
#define MSG(outcome, reason) \
    EPRINTF( \
        outcome ": %s (%s%s): " reason "\n", \
        BINTESTS[bt].test_bin + 2, \
        arch, \
        opt ? ", optimized" : "" \
    )

    switch (support_status(arch)) {
    case ARCH_DISABLED:
        MSG("SKIPPED", "architecture support disabled");
        return TEST_SKIPPED;
    case CANT_RUN:
        MSG("SKIPPED", "can't run target binaries");
        return TEST_SKIPPED;
    case UNKNOWN_ARCH:
        EPRINTF("Unreachable branch at " __FILE__ ":%d reached\n", __LINE__);
        abort();
    default: break;
    }

    size_t nbytes = BINTESTS[bt].expected_sz;
    /* + 3 for ".bf", + 1 for NUL terminator, - 2 for the skipped bytes */
    char *src_name = checked_malloc(strlen(BINTESTS[bt].test_bin) + 2);
    /* set src_name to BINTESTS[bt].test_bin without the leading "./", and with
     * ".bf" appended. */
    strcpy(stpcpy(src_name, BINTESTS[bt].test_bin + 2), ".bf");

    const char **args = ARGS(opt ? "-Oa" : "-a", arch, "--", src_name);

    if (subprocess(args) != EXIT_SUCCESS) {
        MSG("FAILURE", "failed to compile");
        goto fail_before_chld;
    }

    sized_buf chld_output = {
        .buf = checked_malloc(nbytes), .sz = 0, .capacity = nbytes
    };
    run_capturing(
        (const char *[]){BINTESTS[bt].test_bin, NULL}, &chld_output, NULL
    );

    if (nbytes) {
        if (chld_output.sz != nbytes) {
            MSG("FAILURE", "output size mismatch");
            goto fail_after_chld;
        }
        if (memcmp(chld_output.buf, BINTESTS[bt].expected, nbytes) != 0) {
            MSG("FAILURE", "output mismatch");
            goto fail_after_chld;
        }
    }

    free(chld_output.buf);
    free(src_name);
    EPRINTF(
        "SUCCESS: %s (%s%s)\n",
        BINTESTS[bt].test_bin + 2,
        arch,
        opt ? ", optimized" : ""
    );
    CHECKED(unlink(BINTESTS[bt].test_bin) == 0);
    return TEST_SUCCEEDED;

fail_after_chld:
    free(chld_output.buf);
    CHECKED(unlink(BINTESTS[bt].test_bin) == 0);
fail_before_chld:
    free(src_name);
    return TEST_FAILED;
#undef MSG
}

/* test the rw binary - returns a non-zero value if it didn't run successfully
 * for the provided byte value */
static bool rw_test_run(uchar byte, uchar *output_byte) {
    int c2p[2];
    int p2c[2];
    int chld_status;
    uchar overwrite;
    pid_t chld;
    CHECKED(pipe(c2p) == 0);
    CHECKED(pipe(p2c) == 0);
    CHECKED((chld = fork()) != -1);
    if (chld == 0) {
        POST_FORK_CHECKED(close(p2c[1]) == 0);
        POST_FORK_CHECKED(close(c2p[0]) == 0);
        POST_FORK_CHECKED(dup2(p2c[0], STDIN_FILENO) >= 0);
        POST_FORK_CHECKED(dup2(c2p[1], STDOUT_FILENO) >= 0);
        POST_FORK_CHECKED(close(p2c[0]) == 0);
        POST_FORK_CHECKED(close(c2p[1]) == 0);
        POST_FORK_CHECKED(execl("./rw", "./rw", NULL) != -1);
        abort();
    }
    CHECKED(close(c2p[1]) != -1);
    CHECKED(close(p2c[0]) != -1);
    if (write(p2c[1], &byte, 1) != 1) {
        EPRINTF(
            __FILE__ ":%s:%d: failed to write to p2c[1]\n", __func__, __LINE__
        );
        goto fail;
    }

    if (read(c2p[0], output_byte, 1) != 1) {
        EPRINTF(__FILE__ ":%s:%d: no bytes read\n", __func__, __LINE__);
        goto fail;
    }
    if (read(c2p[0], &overwrite, 1) != 0) {
        EPRINTF(
            __FILE__ ":%s:%d: more than one byte read\n", __func__, __LINE__
        );
        goto fail;
    }
    CHECKED(close(c2p[0]) != -1);
    CHECKED(close(p2c[1]) != -1);
    CHECKED(waitpid(chld, &chld_status, 0) == chld);
    if (!WIFEXITED(chld_status)) {
        EPRINTF(
            __FILE__ ":%s:%d: child exited abnormally\n", __func__, __LINE__
        );
        if (WIFSIGNALED(chld_status)) {
            EPRINTF(
                "stopped due to signal %s.\n", strsignal(WTERMSIG(chld_status))
            );
        }
        if (WIFSTOPPED(chld_status)) kill(chld, SIGTERM);
        return false;
    }
    if (WEXITSTATUS(chld_status) != 0) {
        EPRINTF(
            __FILE__ ":%s:%d: nonzero child exit code\n", __func__, __LINE__
        );
        return false;
    }
    return true;
fail:
    CHECKED(kill(chld, SIGTERM) == 0);
    CHECKED(close(c2p[0]) != -1);
    CHECKED(close(p2c[1]) != -1);
    return false;
}

static test_outcome rw_test(const char *arch, bool opt) {
    const char *variant = opt ? ", optimized" : "";
#define MSG(outcome, reason) \
    EPRINTF(outcome ": rw (%s%s): " reason "\n", arch, opt ? ", optimized" : "")
    switch (support_status(arch)) {
    case ARCH_DISABLED:
        MSG("SKIPPED", "architecture support disabled");
        return TEST_SKIPPED;
    case CANT_RUN:
        MSG("SKIPPED", "can't run target binaries");
        return TEST_SKIPPED;
    case UNKNOWN_ARCH:
        EPRINTF("Unreachable branch at " __FILE__ ":%d reached\n", __LINE__);
        abort();
    default: break;
    }

    const char **args = ARGS(opt ? "-Oa" : "-a", arch, "rw.bf");

    if (subprocess(args) != EXIT_SUCCESS) {
        MSG("FAILURE", "failed to compile");
        return TEST_FAILED;
    }
    test_outcome ret;

    for (uint i = 0; i < 256; i++) {
        uchar out_byte;
        if (!rw_test_run(i, &out_byte)) {
            EPRINTF(
                "FAILURE: rw (%s%s): abnormal run with byte 0x%02x\n",
                arch,
                variant,
                i
            );
            ret = TEST_FAILED;
            goto cleanup;
        }
        if (out_byte != i) {
            EPRINTF(
                "FAILURE: rw (%s%s): run with byte 0x%02x printed byte "
                "%0x02hhx\n",
                arch,
                variant,
                i,
                out_byte
            );
            ret = TEST_FAILED;
            goto cleanup;
        }
    }

    EPRINTF("SUCCESS: rw (%s%s)\n", arch, variant);
    ret = TEST_SUCCEEDED;
cleanup:
    CHECKED(unlink("rw") == 0);
    return ret;
#undef MSG
}

static bool tm_test_run(char outbuf[2][16]) {
    int c2p[2];
    int p2c[2];
    int chld_status;
    pid_t chld;
    char inputs[] = "01";
    for (int iter = 0; inputs[iter]; iter++) {
        CHECKED(pipe(c2p) == 0);
        CHECKED(pipe(p2c) == 0);
        CHECKED((chld = fork()) != -1);
        if (chld == 0) {
            signal(SIGPIPE, SIG_DFL);
            POST_FORK_CHECKED(close(c2p[0]) == 0);
            POST_FORK_CHECKED(close(p2c[1]) == 0);
            POST_FORK_CHECKED(dup2(p2c[0], STDIN_FILENO) >= 0);
            POST_FORK_CHECKED(dup2(c2p[1], STDOUT_FILENO) >= 0);
            POST_FORK_CHECKED(close(p2c[0]) == 0);
            POST_FORK_CHECKED(close(c2p[1]) == 0);
            POST_FORK_CHECKED(
                execl("./truthmachine", "./truthmachine", NULL) != -1
            );
        }
        CHECKED(close(c2p[1]) == 0);
        CHECKED(close(p2c[0]) == 0);
        if (write(p2c[1], &inputs[iter], 1) != 1) goto chld_fail;
        CHECKED(close(p2c[1]) == 0);
        if (iter == 0) {
            if (read(c2p[0], outbuf[0], 16) != 1) goto chld_fail;
            if (waitpid(chld, &chld_status, 0) != chld) goto fail;
            if (!WIFEXITED(chld_status)) goto fail;
            if (WEXITSTATUS(chld_status) != 0) goto fail;
            CHECKED(close(c2p[0]) == 0);
        } else {
            /* can't do read(.., 16) because child process's output is not
             * buffered and less than 16 bytes could have been written. */
            for (ifast_8 i = 0; i < 16; i++) {
                if (read(c2p[0], &outbuf[1][i], 1) != 1) goto chld_fail;
            }
            CHECKED(close(c2p[0]) == 0);
            CHECKED(waitpid(chld, &chld_status, 0) != -1);
            if (!WIFSIGNALED(chld_status)) goto fail;
            if (WTERMSIG(chld_status) != SIGPIPE) goto fail;
        }
    }
    return true;
chld_fail:
    kill(chld, SIGTERM);
fail:
    close(c2p[0]);
    close(p2c[1]);
    return false;
}

static test_outcome tm_test(const char *arch, bool opt) {
#define MSG(outcome, reason) \
    EPRINTF( \
        outcome ": truthmachine (%s%s): " reason "\n", \
        arch, \
        opt ? ", optimized" : "" \
    )

    switch (support_status(arch)) {
    case ARCH_DISABLED:
        MSG("SKIPPED", "architecture support disabled");
        return TEST_SKIPPED;
    case CANT_RUN:
        MSG("SKIPPED", "can't run target binaries");
        return TEST_SKIPPED;
    case UNKNOWN_ARCH:
        EPRINTF("Unreachable branch at " __FILE__ ":%d reached\n", __LINE__);
        abort();
    default: break;
    }
    const char *args[] = {
        EAMBFC, opt ? "-Oa" : "-a", arch, "truthmachine.bf", NULL
    };
    if (subprocess(args) != EXIT_SUCCESS) {
        MSG("FAILURE", "failed to compile");
        return TEST_FAILED;
    }

    test_outcome ret;

    char outbuf[2][16] = {{0}, {0}};
    const char expected[2][16] = {"0", "1111111111111111"};
    if (!tm_test_run(outbuf)) {
        MSG("FAILURE", "abnormal run");
        goto cleanup;
    }
    if (memcmp(expected[0], outbuf[0], 16) != 0) {
        MSG("FAILURE", "input '0' results in output other than single '0'");
        goto cleanup;
    }
    if (memcmp(expected[1], outbuf[1], 16) != 0) {
        MSG("FAILURE", "input '1' results in output other than repeating '1'");
        goto cleanup;
    }
    EPRINTF("SUCCESS: truthmachine (%s%s)\n", arch, opt ? ", optimized" : "");
    ret = TEST_SUCCEEDED;
cleanup:
    CHECKED(unlink("truthmachine") == 0);
    return ret;
#undef MSG
}

static nonnull_args void count_result(
    result_tracker *results, test_outcome result
) {
    switch (result) {
    case TEST_SUCCEEDED: results->succeeded++; break;
    case TEST_FAILED: results->failed++; break;
    case TEST_SKIPPED: results->skipped++;
    }
}

/* run bintests for each supported architecture, ensuring that they work as
 * expected, and updating `results */
static nonnull_args void run_bin_tests(result_tracker *results) {
    for (ifast_8 i = 0; ARCHES[i]; i++) {
        for (ifast_8 bt = 0; bt < NBINTESTS; bt++) {
            count_result(results, bin_test(bt, ARCHES[i], false));
            count_result(results, bin_test(bt, ARCHES[i], true));
        }
        count_result(results, rw_test(ARCHES[i], false));
        count_result(results, rw_test(ARCHES[i], true));
        count_result(results, tm_test(ARCHES[i], false));
        count_result(results, tm_test(ARCHES[i], true));
    }
}

static nonnull_args test_outcome err_test(
    const char *test_id, const char *expected_err, const char *restrict args[]
) {
    size_t i;
    for (i = 1; args[i - 1]; i++);
    const char **moved_args = calloc(i + 1, sizeof(char *));
    if (moved_args == NULL) abort();

    moved_args[0] = args[0];
    moved_args[1] = "-j";
    for (i = 1; args[i]; i++) moved_args[i + 1] = args[i];

    sized_buf out = {.buf = calloc(4096, 1), .sz = 0, .capacity = 4096};
    if (out.buf == NULL) abort();
    if (run_capturing(moved_args, &out, NULL) != EXIT_FAILURE) {
        free(out.buf);
        free(moved_args);
        EPRINTF(
            "FAILURE: %s: eambfc exited successfully, expected error\n", test_id
        );
        return TEST_FAILED;
    }
    free(moved_args);

    char error_id[33] = {0};
    if (sscanf(out.buf, "{\"errorId\": \"%32[^\"]s\", ", error_id) != 1) {
        memset(out.buf + out.sz, 0, out.capacity - out.sz);
        fprintf(stderr, "%4096s\n", out.buf);
        abort();
    }
    free(out.buf);
    if (strcmp(error_id, expected_err) != 0) {
        EPRINTF(
            "FAILURE: %s: expected error \"%s\", got error \"%s\".\n",
            test_id,
            expected_err,
            error_id
        );
        return TEST_FAILED;
    }
    EPRINTF("SUCCESS: %s\n", test_id);
    return TEST_SUCCEEDED;
}

static nonnull_args void run_bad_arg_tests(result_tracker *results) {
    const struct arg_test {
        const char *id;
        const char *expected;
        const char **args;
    } tests[10] = {
        {NULL, "MultipleExtensions", ARGS("-e.brf", "-e.bf", "hello.bf")},
        {NULL, "MultipleTapeBlockCounts", ARGS("-t1", "-t32")},
        {"MissingOperand (-e)", "MissingOperand", ARGS("-e")},
        {"MissingOperand (-t)", "MissingOperand", ARGS("-t")},
        {NULL, "UnknownArg", ARGS("-T")},
        {NULL, "NoSourceFiles", ARGS(NULL)},
        {NULL, "BadSourceExtension", ARGS("test_driver.c")},
        {NULL, "TapeSizeZero", ARGS("-t0")},
        {NULL, "TapeTooLarge", ARGS("-t9223372036854775807")},
        {NULL, "MultipleOutputExtensions", ARGS("-s", ".elf", "-s", ".out")},
    };

    for (int i = 0; i < 10; i++) {
        const char *id = tests[i].id ? tests[i].id : tests[i].expected;
        count_result(results, err_test(id, tests[i].expected, tests[i].args));
    }
}

static nonnull_args void run_perm_err_tests(result_tracker *results) {
    struct stat hello_perms;
    stat("hello.bf", &hello_perms);
    chmod("hello.bf", 0);
    count_result(
        results, err_test("OpenReadFailed", "OpenReadFailed", ARGS("hello.bf"))
    );
    chmod("hello.bf", hello_perms.st_mode);
    int fd;
    CHECKED((fd = creat("hello.b", 0500)) >= 0);
    CHECKED(close(fd) == 0);
    count_result(
        results,
        err_test(
            "OpenWriteFailed", "OpenWriteFailed", ARGS("-e", "f", "hello.bf")
        )
    );
    CHECKED(unlink("hello.b") == 0);
}

typedef nonnull_args test_outcome (*hellotest)(const sized_buf *const);

#define MSG(outcome, reason) EPRINTF(outcome ": %s: " reason "\n", __func__);

static nonnull_args test_outcome
test_unseekable(const sized_buf *const hello_code) {
    CHECKED(mkfifo("unseekable", 0755) == 0);
    CHECKED(symlink("hello.bf", "unseekable.bf") == 0);
    pid_t chld;
    int fifo_fd, chld_status;
    CHECKED((chld = fork()) != -1);
    if (chld == 0) {
        execl(EAMBFC, EAMBFC, "unseekable.bf", NULL);
        /* can't safely call fputs - it's not signal-safe, and only signal-safe
         * functions are guaranteed to have defined behavior between fork and
         * exec. */
        write(
            STDERR_FILENO,
            "Failed to exec eambfc to compile unseekable.bf\n",
            48
        );
        abort();
    }
    CHECKED((fifo_fd = open("unseekable", O_RDONLY)) != -1);
    sized_buf output = {
        .buf = checked_malloc(BFC_CHUNK_SIZE),
        .sz = 0,
        .capacity = BFC_CHUNK_SIZE
    };
    while (read_chunk(&output, fifo_fd));
    CHECKED((waitpid(chld, &chld_status, 0) != -1));
    CHECKED(unlink("unseekable") == 0);
    CHECKED(unlink("unseekable.bf") == 0);
    if (!WIFEXITED(chld_status)) {
        MSG("FAILURE", "eambfc stopped abnormally when compiling unseekable\n");
        free(output.buf);
        return TEST_FAILED;
    }
    if (WEXITSTATUS(chld_status) != EXIT_SUCCESS) {
        MSG("FAILURE", "eambfc exit status indicates failure");
        free(output.buf);
        return TEST_FAILED;
    }
    if (sb_eq(&output, hello_code)) {
        fputs("SUCCESS: unseekable\n", stderr);
        free(output.buf);
        return TEST_SUCCEEDED;
    } else {
        MSG("FAILURE", "output mismatch");
        free(output.buf);
        return TEST_FAILED;
    }
}

static nonnull_args test_outcome
alternate_extension(const sized_buf *const hello_code) {
    CHECKED(symlink("hello.bf", "alternate_extension.brnfck") == 0);
    const char **args = ARGS("-e", ".brnfck", "alternate_extension.brnfck");
    if (subprocess(args) != EXIT_SUCCESS) {
        MSG("FAILURE", "eambfc returned nonzero exit code");
        CHECKED(unlink("alternate_extension.brnfck") == 0);
        return TEST_FAILED;
    }
    CHECKED(unlink("alternate_extension.brnfck") == 0);
    sized_buf output = {
        .buf = checked_malloc(BFC_CHUNK_SIZE),
        .sz = 0,
        .capacity = BFC_CHUNK_SIZE
    };
    int fd;
    CHECKED((fd = open("alternate_extension", O_RDONLY)) != -1);
    while (read_chunk(&output, fd));
    CHECKED(close(fd) == 0);
    CHECKED(unlink("alternate_extension") == 0);
    if (sb_eq(&output, hello_code)) {
        fputs("SUCCESS: alternate_extension\n", stderr);
        free(output.buf);
        return TEST_SUCCEEDED;
    } else {
        MSG("FAILURE", "output mismatch");
        free(output.buf);
        return TEST_FAILED;
    }
}

static nonnull_args test_outcome out_suffix(const sized_buf *const hello_code) {
    const char **args = ARGS("-s", ".elf", "hello.bf");
    if (subprocess(args) != EXIT_SUCCESS) {
        MSG("FAILURE", "eambfc returned nonzero exit code");
        return TEST_FAILED;
    }
    sized_buf output = {
        .buf = checked_malloc(BFC_CHUNK_SIZE),
        .sz = 0,
        .capacity = BFC_CHUNK_SIZE
    };

    int fd;
    CHECKED((fd = open("hello.elf", O_RDONLY)) != -1);
    while (read_chunk(&output, fd));
    CHECKED(close(fd) == 0);
    CHECKED(unlink("hello.elf") == 0);

    if (sb_eq(&output, hello_code)) {
        fputs("SUCCESS: out_suffix\n", stderr);
        free(output.buf);
        return TEST_SUCCEEDED;
    } else {
        MSG("FAILURE", "output mismatch");
        free(output.buf);
        return TEST_FAILED;
    }
}

static nonnull_args test_outcome piped_in(const sized_buf *const hello_code) {
    CHECKED(mkfifo("piped_in.bf", 0755) == 0);
    pid_t chld;
    CHECKED((chld = fork()) != -1);
    if (chld == 0) {
        execl(EAMBFC, EAMBFC, "piped_in.bf", NULL);
        write(
            STDERR_FILENO, "Failed to exec eambfc to compile piped_in.bf\n", 45
        );
        abort();
    }
    int fifo_fd, hello_fd, chld_status;
    CHECKED((hello_fd = open("hello.bf", O_RDONLY)) != -1);
    CHECKED((fifo_fd = open("piped_in.bf", O_WRONLY)) != -1);
    char transfer[BFC_CHUNK_SIZE];
    ssize_t sz;
    while ((sz = read(hello_fd, transfer, BFC_CHUNK_SIZE))) {
        CHECKED(sz != -1);
        CHECKED(write(fifo_fd, transfer, sz) == sz);
    }
    CHECKED(close(hello_fd) == 0);
    CHECKED(close(fifo_fd) == 0);
    CHECKED(waitpid(chld, &chld_status, 0) == chld);
    if (!WIFEXITED(chld_status) || WEXITSTATUS(chld_status) != EXIT_SUCCESS) {
        MSG("FAILURE", "eambfc didn't exit successfully\n");
        return TEST_FAILED;
    }
    sized_buf output = {
        .buf = checked_malloc(BFC_CHUNK_SIZE),
        .sz = 0,
        .capacity = BFC_CHUNK_SIZE
    };
    int fd;
    CHECKED((fd = open("piped_in", O_RDONLY)) != -1);
    while (read_chunk(&output, fd));
    CHECKED(close(fd) == 0);
    CHECKED(unlink("piped_in") == 0);
    CHECKED(unlink("piped_in.bf") == 0);

    if (sb_eq(&output, hello_code)) {
        fputs("SUCCESS: piped_in\n", stderr);
        free(output.buf);
        return TEST_SUCCEEDED;
    } else {
        MSG("FAILURE", "output mismatch");
        free(output.buf);
        return TEST_FAILED;
    }
}

static nonnull_args void run_alt_hello_tests(result_tracker *results) {
    const hellotest test_funcs[] = {
        test_unseekable,
        alternate_extension,
        out_suffix,
        piped_in,
        NULL,
    };
    if (subprocess(ARGS("hello.bf")) != EXIT_SUCCESS) {
        fputs("failed to compile hello.bf for comparisons\n", stderr);
        abort();
    }
    sized_buf hello = {
        .buf = checked_malloc(BFC_CHUNK_SIZE),
        .sz = 0,
        .capacity = BFC_CHUNK_SIZE
    };
    int fd;
    CHECKED((fd = open("hello", O_RDONLY)) != -1);
    while (read_chunk(&hello, fd));
    CHECKED(close(fd) == 0);
    CHECKED(unlink("hello") == 0);
    for (int i = 0; test_funcs[i] != NULL; i++) {
        count_result(results, test_funcs[i](&hello));
    }

    free(hello.buf);
}

static nonnull_args test_outcome dead_code(void) {
    if (subprocess(ARGS("-O", "null.bf", "dead_code.bf")) != EXIT_SUCCESS) {
        MSG("FAILURE", "failed to compile hello.bf for comparisons");
        CHECKED(unlink("null") == 0 || errno == ENOENT);
        CHECKED(unlink("dead_code") == 0 || errno == ENOENT);
        return EXIT_FAILURE;
    }
    int fd;

    sized_buf dead_code_output = {
        .buf = checked_malloc(BFC_CHUNK_SIZE),
        .sz = 0,
        .capacity = BFC_CHUNK_SIZE
    };
    CHECKED((fd = open("dead_code", O_RDONLY)) != -1);
    while (read_chunk(&dead_code_output, fd));
    CHECKED(close(fd) == 0);

    sized_buf null_output = {
        .buf = checked_malloc(BFC_CHUNK_SIZE),
        .sz = 0,
        .capacity = BFC_CHUNK_SIZE
    };
    CHECKED((fd = open("null", O_RDONLY)) != -1);
    while (read_chunk(&null_output, fd));
    CHECKED(close(fd) == 0);

    bool succeeded = sb_eq(&dead_code_output, &null_output);

    free(dead_code_output.buf);
    free(null_output.buf);
    CHECKED(unlink("null") == 0 || errno == ENOENT);
    CHECKED(unlink("dead_code") == 0 || errno == ENOENT);
    if (succeeded) {
        fputs("SUCCESS: dead_code\n", stderr);
        return TEST_SUCCEEDED;
    } else {
        MSG("FAILURE", "dead_code not optimiazed down equivalent to nothing");
        return TEST_FAILED;
    }
}

#undef MSG

static nonnull_args void run_misc_tests(result_tracker *results) {
    count_result(results, dead_code());
    const char *unmatched_types[2][2] = {
        {"UnmatchedOpen", "unmatched_open.bf"},
        {"UnmatchedClose", "unmatched_close.bf"}
    };
    for (int i = 0; i < 2; i++) {
        count_result(
            results,
            err_test(
                unmatched_types[i][0],
                unmatched_types[i][0],
                ARGS(unmatched_types[i][1])
            )
        );
        count_result(
            results,
            err_test(
                unmatched_types[i][0],
                unmatched_types[i][0],
                ARGS("-O", unmatched_types[i][1])
            )
        );
    }
}

int main(int argc, const char *argv[]) {
    if (!argc) abort();
    if (!strchr(argv[0], '/')) abort();
    char *argv0 = strdup(argv[0]);
    if (argv0 == NULL) abort();
    CHECKED(chdir(dirname(argv0)) == 0);
    free(argv0);

    signal(SIGPIPE, SIG_IGN);
    EAMBFC = getenv("EAMBFC");
    if (EAMBFC == NULL) EAMBFC = "../eambfc";
    load_arch_support();
    result_tracker results = {0, 0, 0};
    run_bad_arg_tests(&results);
    run_perm_err_tests(&results);
    run_bin_tests(&results);
    run_alt_hello_tests(&results);
    run_misc_tests(&results);
    printf(
        "\n#################\nRESULTS\n\n"
        "SUCCESSES: %" PRIu8 "\nFAILURES:  %" PRIu8 "\nSKIPPED:   %" PRIu8 "\n",
        results.succeeded,
        results.failed,
        results.skipped
    );
    /* clang-format on */
    if (results.failed || (getenv("BFC_DONT_SKIP_TESTS") && results.skipped)) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
