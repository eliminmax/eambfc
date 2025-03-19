/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

/* C99 */
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
/* internal */
#include "../attributes.h"
#include "../config.h"
#include "../types.h"
#include "colortest_output.h"

#define EPRINTF(fmt, ...) fprintf(stderr, fmt, __VA_ARGS__)

/* alternative to using non-portable, non-standard `asprintf` for new strings
 * (snprintf to NULL with size zero is stated by the C99 standard to be allowed,
 * and is defined to return the number of bytes that would have been added) */
#define SPRINTF_NEW(dst, fmt, ...) \
    do { \
        dst = malloc(snprintf(NULL, 0, fmt, __VA_ARGS__) + 1); \
        if (dst == NULL) abort(); \
        sprintf(dst, fmt, __VA_ARGS__); \
    } while (0)

static const char *EAMBFC;
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

typedef struct bintest {
    const char *test_bin;
    const char *expected;
    size_t expected_sz;
} bintest;

static const bintest BINTESTS[] = {
    {"colortest", COLORTEST_OUTPUT, COLORTEST_OUTPUT_LEN},
    {"hello", "Hello, world!\n", 14},
    {"loop", "!", 1},
    {"null", "", 0},
    {"wrap", "\xf0\x9f\xa7\x9f" /* utf8-encoded zombie emoji */, 4},
    {"wrap2", "0000", 4},
};

#define NBINTESTS 6

typedef enum test_outcome {
    TEST_FAILED = -1,
    TEST_SUCCEEDED = 0,
    TEST_SKIPPED = 1,
} test_outcome;

static nonnull_args size_t read_chunk(sized_buf *dst, int fd) {
    if (dst->capacity > (SIZE_MAX - 0x8080)) abort();
    char buf[128];
    ssize_t ct = read(fd, buf, 128);
    if (ct < 0) abort();
    size_t needed_cap = dst->sz + ct;
    if (needed_cap > dst->capacity) {
        if (needed_cap & 0xfff) needed_cap = (needed_cap + 0x1000) & (~0xfff);
        char *newbuf = realloc(dst->buf, needed_cap);
        if (newbuf == NULL) abort();
        dst->buf = newbuf;
    }
    memcpy(dst->buf + dst->sz, buf, ct);
    dst->sz += ct;
    return ct;
}

static nonnull_arg(1) int exec_eambfc(
    const char *args[], sized_buf *out, sized_buf *err
) {
    int chld_status;
    pid_t chld;
    int out_pipe[2];
    int err_pipe[2];
    if (out != NULL && pipe(out_pipe) != 0) abort();
    if (err != NULL && pipe(err_pipe) != 0) abort();
    switch (chld = fork()) {
    case -1: abort();
    case 0:
        if (out != NULL) {
            dup2(out_pipe[1], STDOUT_FILENO);
            close(out_pipe[0]);
            close(out_pipe[1]);
        }
        if (err != NULL) {
            dup2(err_pipe[1], STDERR_FILENO);
            close(err_pipe[0]);
            close(err_pipe[1]);
        }
        /* This cast is risky, but the POSIX standard explicitly documents that
         * args are not modified, and the use of `char *const[]` instead of
         * `const char *const[]` is for compatibility with existing code calling
         * the execv* variants of exec.
         *
         * As long as the data is not actually modified, no UB occurs. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
        execv(EAMBFC, (char *const *)args);
#pragma GCC diagnostic pop
        abort();
    default:
        if (out) close(out_pipe[1]);
        if (err) close(err_pipe[1]);
        while ((out && read_chunk(out, out_pipe[0])) ||
               (err && read_chunk(err, err_pipe[0])));
        if (waitpid(chld, &chld_status, 0) == -1) abort();
        return WIFEXITED(chld_status) ? WEXITSTATUS(chld_status) : -1;
    }
}

#define FAIL(test_id, reason, label) \
    do { \
        EPRINTF("FAILURE: %s: " reason "\n", test_id); \
        goto label; \
    } while (0)

/* test a binary which takes no input, making sure it exists successfully after
 * writing the expected data to stdout */
static test_outcome bin_test(ifast_8 bt, const char *restrict arch, bool opt) {
    char *test_id;
    SPRINTF_NEW(
        test_id,
        "%s (%s%s)",
        BINTESTS[bt].test_bin,
        arch,
        opt ? ", optimized" : ""
    );

    switch (support_status(arch)) {
    case ARCH_DISABLED:
        EPRINTF("SKIPPED: %s: %s support disabled\n", test_id, arch);
        free(test_id);
        return TEST_SKIPPED;
    case CANT_RUN:
        EPRINTF("SKIPPED: %s: can't run %s binaries\n", test_id, arch);
        free(test_id);
        return TEST_SKIPPED;
    case UNKNOWN_ARCH: abort();
    default: break;
    }

    size_t nbytes = BINTESTS[bt].expected_sz;
    char *test_filename;
    SPRINTF_NEW(test_filename, "%s.bf", BINTESTS[bt].test_bin);
    const char *args[] = {
        EAMBFC, "-j", opt ? "-Oa" : "-a", arch, "--", test_filename, NULL
    };

    if (exec_eambfc(args, NULL, NULL) != EXIT_SUCCESS) {
        FAIL(test_id, "Failed to compile", fail_before_chld);
    }

    char *chld_output = malloc(nbytes ? nbytes : 1);
    if (chld_output == NULL) abort();

    char dummy;
    char *cmd;
    SPRINTF_NEW(cmd, "./%s", BINTESTS[bt].test_bin);
    FILE *chld = popen(cmd, "r");

    if (nbytes && fread(chld_output, 1, nbytes, chld) != nbytes) {
        FAIL(test_id, "not enough output", fail_with_chld);
    }

    if (fread(&dummy, 1, 1, chld) > 0 || ferror(chld)) {
        FAIL(test_id, "too much output", fail_with_chld);
    }

    if (pclose(chld) != EXIT_SUCCESS) {
        FAIL(test_id, "child process failed", fail_after_chld);
    }

    if (nbytes && memcmp(chld_output, BINTESTS[bt].expected, nbytes) != 0) {
        FAIL(test_id, "output mismatch", fail_after_chld);
    }

    free(cmd);
    free(chld_output);
    free(test_filename);
    EPRINTF("SUCCESS: %s\n", test_id);
    free(test_id);
    /* both exit_status and cmp_status should be zero on success */
    return TEST_SUCCEEDED;

fail_with_chld:
    pclose(chld);
fail_after_chld:
    free(chld_output);
    free(cmd);
fail_before_chld:
    free(test_filename);
    free(test_id);
    return TEST_FAILED;
}

/* test the rw binary - returns a non-zero value if it didn't run successfully
 * for the provided byte value */
static bool rw_test_run(uchar byte, uchar *output_byte) {
    int c2p[2];
    int p2c[2];
    int chld_status;
    uchar overwrite;
    pid_t chld;
    if (pipe(c2p) != 0) abort();
    if (pipe(p2c) != 0) abort();
    switch (chld = fork()) {
    case -1: abort();
    case 0:
        dup2(p2c[0], STDIN_FILENO);
        dup2(c2p[1], STDOUT_FILENO);
        close(c2p[0]);
        close(c2p[1]);
        close(p2c[0]);
        close(p2c[1]);
        execl("./rw", "./rw", NULL);
        abort();
    default:
        close(c2p[1]);
        close(p2c[0]);
        if (write(p2c[1], &byte, 1) != 1) goto fail;
        wait(&chld_status);
        if (!WIFEXITED(chld_status)) goto fail;
        if (WEXITSTATUS(chld_status) != 0) goto fail;
        if (read(c2p[0], output_byte, 1) != 1) goto fail;
        if (read(c2p[0], &overwrite, 1) != 0) goto fail;
        close(c2p[0]);
        close(p2c[1]);
        return true;
    }
fail:
    close(c2p[0]);
    close(p2c[1]);
    return false;
}

static test_outcome rw_test(const char *arch, bool opt) {
    char *test_id;
    SPRINTF_NEW(test_id, "rw (%s%s)", arch, opt ? ", optimized" : "");
    switch (support_status(arch)) {
    case ARCH_DISABLED:
        EPRINTF("SKIPPED: %s: %s support disabled\n", test_id, arch);
        free(test_id);
        return TEST_SKIPPED;
    case CANT_RUN:
        EPRINTF("SKIPPED: %s: can't run %s binaries\n", test_id, arch);
        free(test_id);
        return TEST_SKIPPED;
    case UNKNOWN_ARCH: abort();
    default: break;
    }

    const char *args[] = {
        EAMBFC, "-j", opt ? "-Oa" : "-a", arch, "rw.bf", NULL
    };
    if (exec_eambfc(args, NULL, NULL) != EXIT_SUCCESS) {
        FAIL(test_id, "Failed to compile", fail);
    }

    for (uint i = 0; i < 256; i++) {
        uchar out_byte;
        if (!rw_test_run(i, &out_byte)) {
            EPRINTF("FAILURE: %s: abnormal run with byte 0x%02x\n", test_id, i);
            goto fail;
        }
        if (out_byte != i) {
            EPRINTF(
                "FAILURE: %s: run with byte 0x%02x printed byte %0x02hhx\n",
                test_id,
                i,
                out_byte
            );
            goto fail;
        }
    }

    EPRINTF("SUCCESS: %s\n", test_id);
    free(test_id);
    return TEST_SUCCEEDED;
fail:
    free(test_id);
    return TEST_FAILED;
}

static bool tm_test_run(char outbuf[2][16]) {
    int c2p[2];
    int p2c[2];
    int chld_status;
    pid_t chld;
    char inputs[] = "01";
    signal(SIGPIPE, SIG_DFL);
    for (int iter = 0; inputs[iter]; iter++) {
        if (pipe(c2p) != 0) abort();
        if (pipe(p2c) != 0) abort();
        switch (chld = fork()) {
        case -1: abort();
        case 0:
            dup2(p2c[0], STDIN_FILENO);
            dup2(c2p[1], STDOUT_FILENO);
            close(c2p[0]);
            close(c2p[1]);
            close(p2c[0]);
            close(p2c[1]);
            execl("./truthmachine", "./truthmachine", NULL);
            abort();
        default:
            close(c2p[1]);
            close(p2c[0]);
            if (write(p2c[1], &inputs[iter], 1) != 1) goto chld_fail;
            close(p2c[1]);
            if (iter == 0) {
                if (read(c2p[0], outbuf[0], 16) != 1) goto chld_fail;
                wait(&chld_status);
                if (!WIFEXITED(chld_status)) goto fail;
                if (WEXITSTATUS(chld_status) != 0) goto fail;
                close(c2p[0]);
            } else {
                /* can't do read(.., 16) because child process's output is not
                 * buffered and less than 16 bytes could have been written. */
                for (ifast_8 i = 0; i < 16; i++) {
                    if (read(c2p[0], &outbuf[1][i], 1) != 1) goto chld_fail;
                }
                close(c2p[0]);
                wait(&chld_status);
                if (!WIFSIGNALED(chld_status)) goto fail;
                if (WTERMSIG(chld_status) != SIGPIPE) goto fail;
            }
        }
    }
    signal(SIGPIPE, SIG_IGN);
    return true;
chld_fail:
    kill(chld, SIGTERM);
fail:
    close(c2p[0]);
    close(p2c[1]);
    signal(SIGPIPE, SIG_IGN);
    return false;
}

static test_outcome tm_test(const char *arch, bool opt) {
    char *test_id;
    SPRINTF_NEW(test_id, "truthmachine (%s%s)", arch, opt ? ", optimized" : "");
    switch (support_status(arch)) {
    case ARCH_DISABLED:
        EPRINTF("SKIPPED: %s: %s support disabled\n", test_id, arch);
        free(test_id);
        return TEST_SKIPPED;
    case CANT_RUN:
        EPRINTF("SKIPPED: %s: can't run %s binaries\n", test_id, arch);
        free(test_id);
        return TEST_SKIPPED;
    case UNKNOWN_ARCH: abort();
    default: break;
    }
    const char *args[] = {
        EAMBFC, "-j", opt ? "-Oa" : "-a", arch, "truthmachine.bf", NULL
    };
    if (exec_eambfc(args, NULL, NULL) != EXIT_SUCCESS) {
        FAIL(test_id, "Failed to compile", fail);
    }

    char outbuf[2][16] = {{0}, {0}};
    const char expected[2][16] = {"0", "1111111111111111"};
    if (!tm_test_run(outbuf)) FAIL(test_id, "abnormal run", fail);
    if (memcmp(expected[0], outbuf[0], 16) != 0) {
        FAIL(
            test_id, "input '0' results in output other than single '0'", fail
        );
    }
    if (memcmp(expected[1], outbuf[1], 16) != 0) {
        FAIL(
            test_id,
            "input '1' results in output other than repeating '1'",
            fail
        );
    }
    EPRINTF("SUCCESS: %s\n", test_id);
    free(test_id);
    return TEST_SUCCEEDED;

fail:
    free(test_id);
    return TEST_FAILED;
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

static nonnull_args void arch_tests(result_tracker *results) {
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

int main(void) {
    EAMBFC = getenv("EAMBFC");
    if (EAMBFC == NULL) EAMBFC = "../eambfc";
    load_arch_support();
    result_tracker results = {0, 0, 0};
    arch_tests(&results);
    /* clang-format off */
    signal(SIGPIPE, SIG_IGN);
    printf(
        "\n#################\nRESULTS\n"
        "\nSUCCESSES: %" PRIu8
        "\nFAILURES:  %" PRIu8
        "\nSKIPPED:   %" PRIu8
        "\nTOTAL:     %d\n",
        results.succeeded, results.failed, results.skipped,
        results.succeeded + results.failed + results.skipped
    );
    /* clang-format on */
    if (results.failed || (getenv("BFC_DONT_SKIP_TESTS") && results.skipped)) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
