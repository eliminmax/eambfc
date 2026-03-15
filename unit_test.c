/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Utility functions for unit testing, and the unit testing entry point */

#ifdef BFC_TEST
/* C99 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* POSIX */
#include <dlfcn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

/* CUnit */
#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>

#define UNIT_TEST_C 1
/* internal */
#include <types.h>

#include "err.h"
#include "unit_test.h"
#include "util.h"

typedef void (*VoidFn)(void);
typedef void (*GetVersionFn)(uint *, uint *, uint *);
typedef int (*SetOptionFn)(void *ctx, u64 options);
typedef int (*DisasmDisposeFn)(void *ctx);
typedef size_t (*DisasmInstructionFn)(
    void *ctx, u8 *bytes, u64 bytes_sz, u64 pc, char *output, size_t output_sz
);
typedef int (*OpInfoCallbackFn)(
    void *, uint64_t, uint64_t, uint64_t, uint64_t, int, void *
);
typedef const char *(*SymbolLookupCallback)(
    void *, uint64_t, uint64_t *, uint64_t, const char **
);
typedef nonnull_arg(1, 2, 3) void *(*CreateDisasmCPUFeaturesFn)(
    const char *triple,
    const char *cpu,
    const char *features,
    void *,
    int,
    int (*)(void *, uint64_t, uint64_t, uint64_t, uint64_t, int, void *),
    const char *(*)(void *, uint64_t, uint64_t *, uint64_t, const char **)
);

struct llvm_syms {
    void *handle;
    CreateDisasmCPUFeaturesFn CreateDisasmCPUFeatures;
    DisasmDisposeFn DisasmDispose;
    DisasmInstructionFn DisasmInstruction;
    GetVersionFn GetVersion;
    SetOptionFn SetDisasmOptions;
#define ARCH_DISASM_INIT_SYM(SYM) \
    VoidFn Initialize##SYM##TargetInfo; \
    VoidFn Initialize##SYM##TargetMC; \
    VoidFn Initialize##SYM##Disassembler;
#include "backends.h"
    VoidFn Shutdown;
    bool init;
} LLVM = {.handle = NULL, .init = false};

/* ultra-pedantically-POSIXly-correct way to attempt to print messages to stderr
 * between fork and exec, without any async-signal-unsafe operations, while
 * preserving errno */
#define WRITE_ERR(msg) \
    do { \
        int _saved_errno = errno; \
        write(STDERR_FILENO, msg, sizeof(msg) - 1); \
        errno = _saved_errno; \
    } while (0)

noreturn static cold void pipe_out_soname(
    int pipes[2], int epipes[2], const char *llvm_config
) {
    int err;

    /* needed due to GCC -Wanalyzer-fd-leak false positive on dup2
     * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109839 */
#pragma GCC diagnostic push
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wanalyzer-fd-leak"
#endif
    if (dup2(pipes[1], STDOUT_FILENO) < 0) {
        WRITE_ERR("failed to set up stdout pipe");
        goto fail;
    }
#pragma GCC diagnostic pop

    close(pipes[0]);
    close(pipes[1]);
    close(epipes[0]);
    execlp(llvm_config, llvm_config, "--libfiles", (char *)NULL);
    WRITE_ERR("failed to determine soname with llvm-config");
fail:
    err = errno;
    write(epipes[1], &err, sizeof(int));
    close(epipes[1]);
    _Exit(EXIT_FAILURE);
}

static cold char *soname_from_cli(void) {
    const char *llvm_config = getenv("BFC_TEST_LLVM_CONFIG_BIN");
    if (!llvm_config) llvm_config = "llvm-config";
    pid_t chld;
    int pipes[2];
    int epipes[2];
    if (pipe(pipes) == -1) {
        perror("WARNING: failed to set up pipes");
        return NULL;
    }

    if (pipe(epipes) == -1) {
        perror("WARNING: failed to set up pipes");
        close(pipes[0]);
        close(pipes[1]);
        return NULL;
    }

    if ((chld = fork()) == -1) {
        perror("WARNING: failed to fork llvm-config process");
        close(pipes[0]);
        close(pipes[1]);
        close(epipes[0]);
        close(epipes[1]);
        return NULL;
    }

    if (chld == 0) pipe_out_soname(pipes, epipes, llvm_config);

    union read_result result;
    close(pipes[1]);
    close(epipes[1]);
    if (!read_to_sb(pipes[0], &result)) {
        fprintf(
            stderr,
            "unable to read SONAME from child process: %s\n",
            result.err.msg.ref
        );
        kill(chld, SIGTERM);
        goto err_cleanup_pre_soname;
    }
    SizedBuf sb = result.sb;
    int errno_val;
    int status;
    char *soname = sb.buf;
    /* replace the trailing newline with a null terminator */
    soname[sb.sz - 1] = 0;

    if (waitpid(chld, &status, 0) != -1) {
        perror("failed to wait for child process");
        goto err_cleanup;
    }

    if (!WIFEXITED(status)) {
        fprintf(stderr, "%s didn't exit normally\n", llvm_config);
        if (WIFSTOPPED(status)) kill(chld, SIGTERM);
        goto err_cleanup;
    }

    if (WEXITSTATUS(status) == EXIT_SUCCESS) {
        return checked_realloc(soname, sb.sz);
    }

    if (read(epipes[0], &errno_val, sizeof(int)) == sizeof(int)) {
        errno = errno_val;
        perror("llvm-config process setup error");
    } else {
        fputs("llvm-config child process failed\n", stderr);
    }

err_cleanup:
    free(soname);
err_cleanup_pre_soname:
    close(pipes[0]);
    close(epipes[0]);
    return NULL;
}

bool llvm_ok(void) {
    return LLVM.init;
}

static void *llvm_handle(void) {
    if (getenv("BFC_TEST_SKIP_LLVM")) return NULL;
    void *handle;
    char *soname;

    if ((soname = getenv("BFC_TEST_LLVM_SONAME"))) {
        handle = dlopen(soname, RTLD_LAZY);
        if (!handle) {
            fprintf(
                stderr,
                "FATAL: unable to dlopen %s specified in environment "
                "variable BFC_TEST_LLVM_SONAME: %s\n",
                soname,
                dlerror()
            );
            abort();
        }
        return handle;
    }

    const char *fallback_sonames[] = {
        "libLLVM-22.so", "libLLVM-21.so", "libLLVM-20.so", "libLLVM-19.so", NULL
    };
    for (int i = 0; fallback_sonames[i]; i++) {
        if ((handle = dlopen(fallback_sonames[i], RTLD_LAZY))) return handle;
    }

    if ((soname = soname_from_cli())) {
        if (!(handle = dlopen(soname, RTLD_LAZY))) {
            fprintf(
                stderr,
                "Unable to dlopen %s (specified by llvm-config): %s\n",
                soname,
                dlerror()
            );
            handle = NULL;
        }
        free(soname);
        return handle;
    }
    return NULL;
}

static void load_syms(void) {
    if (LLVM.init) return;
    if (!(LLVM.handle = llvm_handle())) goto load_failed;

#define RESOLVE(SYM) \
    do { \
        void *_sym; \
        if (!(_sym = dlsym(LLVM.handle, "LLVM" #SYM))) { \
            fprintf( \
                stderr, \
                "failed to load symbol " #SYM " from libLLVM: %s.\n", \
                dlerror() \
            ); \
            goto load_failed; \
        } \
        *(void **)(&LLVM.SYM) = _sym; \
    } while (0)
    RESOLVE(CreateDisasmCPUFeatures);
    RESOLVE(DisasmDispose);
    RESOLVE(DisasmInstruction);
    RESOLVE(GetVersion);
    RESOLVE(SetDisasmOptions);
#define ARCH_DISASM_INIT_SYM(SYM) \
    RESOLVE(Initialize##SYM##Disassembler); \
    RESOLVE(Initialize##SYM##TargetInfo); \
    RESOLVE(Initialize##SYM##TargetMC);
#include "backends.h"
    RESOLVE(Shutdown);

    LLVM.init = true;
    return;
load_failed:
    LLVM = (struct llvm_syms){0};
}

static void deinit_llvm(void) {
    if (!LLVM.init) return;
    int err = dlclose(LLVM.handle);
    if (err) {
        fprintf(stderr, "WARNING: Error %d while de-initializing LLVM.\n", err);
    }
    LLVM = (struct llvm_syms){0};
}

#define SET_DIS_OPTIONS(ref, opt) \
    if (!LLVM.SetDisasmOptions(ref, opt)) exit(EXIT_FAILURE);

static void llvm_init(void) {
    static bool LLVM_IS_INIT = false;
    if (LLVM_IS_INIT) return;
    load_syms();
    if (!LLVM.init) {
        fputs(
            "Unable to load LLVM. Disassembly tests will be skipped\n", stderr
        );
        return;
    }
#define ARCH_DISASM_INIT_SYM(SYM) \
    LLVM.Initialize##SYM##TargetInfo(); \
    LLVM.Initialize##SYM##TargetMC(); \
    LLVM.Initialize##SYM##Disassembler();
#define ARCH_DISASM(ref, triple, features) \
    ref = LLVM.CreateDisasmCPUFeatures( \
        triple, "generic", features, NULL, 0, NULL, NULL \
    ); \
    if (!ref) { \
        fputs( \
            "Failed to initialize " #ref " with triple " #triple \
            " and features " #features ".\n", \
            stderr \
        ); \
        abort(); \
    }

#include "backends.h"
    /* use Intel assembly syntax - needs to be set before hex_imms, otherwise it
     * overwrites it */
#if BFC_TARGET_I386
    SET_DIS_OPTIONS(I386_DIS, 4 /* AsmPrinterVariant */);
#endif
#if BFC_TARGET_X86_64
    SET_DIS_OPTIONS(X86_64_DIS, 4 /* AsmPrinterVariant */);
#endif

#define ARCH_DISASM(ref, ...) SET_DIS_OPTIONS(ref, 2 /* PrintImmHex */);
#include "backends.h"
    LLVM_IS_INIT = true;
}

enum LLVM_MAJOR_RELEASE libllvm_version(void) {
    static enum LLVM_MAJOR_RELEASE release;
    if (!LLVM.init) return INVALID_VERSION;
    static bool set = false;

    if (set) return release;

    unsigned major, minor, patch;

    LLVM.GetVersion(&major, &minor, &patch);

    if (major < 19 || major > 22) {
        unsigned fallback = (major < 19) ? 19 : 22;
        fprintf(
            stderr,
            "WARNING: unit_test_driver compiled against unsupported LLVM "
            "version %u.%u.%u. Due to differences in disassembly between LLVM "
            "releases, some tests may fail incorrectly. Falling back to "
            "disassembly for version %u.\n",
            major,
            minor,
            patch,
            fallback
        );
        release = fallback;
        set = true;
        return release;
    } else {
        release = major;
        set = true;
        return release;
    }
}

bool disassemble(void *ref, SizedBuf *bytes, SizedBuf *disasm) {
    assert(LLVM.init);
    char disasm_insn[128];
    disasm->sz = 0;
    while (bytes->sz) {
        memset(disasm_insn, 0, 128);
        size_t used_sz = LLVM.DisasmInstruction(
            ref, (u8 *)bytes->buf, bytes->sz, 0, disasm_insn, 128
        );
        if (!used_sz) return false;
        memmove(bytes->buf, (char *)bytes->buf + used_sz, bytes->sz - used_sz);
        bytes->sz -= used_sz;
        ufast_8 i;
        /* start at 1 to skip leading '\t' */
        /* Replace spaces with tabs. Don't need to explicitly check for the end
         * of the array because LLVMDisasmInstruction always null-terminates is
         * output. */
        for (i = 1; disasm_insn[i]; i++) {
            if (disasm_insn[i] == '\t') disasm_insn[i] = ' ';
        }
        /* replace null terminator with a newline */
        disasm_insn[i] = '\n';
        /* leave `i` as-is, as the 0-based indexing and the extra '\n' cancel
         * out, and the loop shouldn't be null-terminated yet. */
        append_obj(disasm, &disasm_insn[1], i);
    }
    /* null-terminate the output loop */
    char *terminator = sb_reserve(disasm, 1);
    *terminator = 0;
    return true;
}

static void llvm_cleanup(void) {
    if (!LLVM.init) return;
#define ARCH_DISASM(ref, ...) LLVM.DisasmDispose(ref);
#include "backends.h"
    LLVM.Shutdown();
    deinit_llvm();
}

int run_tests(void) {
    if (atexit(llvm_cleanup)) {
        fputs("Failed to register llvm_cleanup with atexit\n", stderr);
        return EXIT_FAILURE;
    }
    llvm_init();
    quiet_mode();
    CU_set_fail_on_inactive(CU_FALSE);

    BF_ERRCHECKED(CU_initialize_registry());
    BF_ERRCHECKED(register_optimize_tests());
    BF_ERRCHECKED(register_util_tests());
    BF_ERRCHECKED(register_serialize_tests());
    BF_ERRCHECKED(register_err_tests());
    BF_ERRCHECKED(register_compile_tests());

#define ARCH_TEST_REGISTER(func) BF_ERRCHECKED(func());
#include "backends.h"

    testing_err = NOT_TESTING;
    /* Run all tests using the console interface */
    CU_basic_set_mode(CU_BRM_VERBOSE);

    BF_ERRCHECKED(CU_basic_run_tests());
    int ret = CU_get_number_of_tests_failed() ? EXIT_FAILURE : EXIT_SUCCESS;
    CU_cleanup_registry();
    llvm_cleanup();
    return ret;
}
#endif /* BFC_TEST */
