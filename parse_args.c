/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */
/* C99 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <unistd.h>
/* internal */
#include "attributes.h"
#include "err.h"
#include "parse_args.h"
#include "version.h"

/* returns true if strcmp matches s to any strings in its argument,
 * and false otherwise.
 * normal safety concerns around strcmp apply. */
nonnull_args static bool any_match(const char *s, int count, ...) {
    va_list ap;
    va_start(ap, count);
    bool found = false;
    for (int i = 0; i < count; i++) {
        if (!strcmp(s, va_arg(ap, const char *))) {
            found = true;
            break;
        }
    }
    va_end(ap);
    return found;
}

nonnull_args static bool select_inter(
    const char *arch_arg, arch_inter **inter
) {
    /* __BACKENDS__ add a block here */
#if BFC_TARGET_X86_64
    if (any_match(arch_arg, 4, "x86_64", "x64", "amd64", "x86-64")) {
        *inter = &X86_64_INTER;
        return true;
    }
#endif /* BFC_TARGET_X86_64 */
#if BFC_TARGET_RISCV64
    if (any_match(optarg, 2, "riscv64", "riscv")) {
        *inter = &RISCV64_INTER;
        return true;
    }
#endif /* BFC_TARGET_RISCV64 */
#if BFC_TARGET_ARM64
    if (any_match(arch_arg, 2, "arm64", "aarch64")) {
        *inter = &ARM64_INTER;
        return true;
    }
#endif /* BFC_TARGET_ARM64 */
#if BFC_TARGET_S390X
    if (any_match(arch_arg, 3, "s390x", "s390", "z/architecture")) {
        *inter = &S390X_INTER;
        return true;
    }
#endif /* BFC_TARGET_S390X */
    param_err("UNKNOWN_ARCH", "{} is not a recognized architecture", arch_arg);
    return false;
}

/* print the help message to outfile. progname should be argv[0]. */
static nonnull_args void show_help(FILE *outfile, const char *progname) {
    fprintf(
        outfile,
        "Usage: %s [options] <program.bf> [<program2.bf> ...]\n\n"
        " -h        - display this help text and exit\n"
        " -V        - print version information and exit\n"
        " -j        - print errors in JSON format*\n"
        "             (assumes file names are UTF-8-encoded.)\n"
        " -q        - don't print errors unless -j was passed*\n"
        " -O        - enable optimization**.\n"
        " -k        - keep files that failed to compile (for debugging)\n"
        " -c        - continue to the next file instead of quitting if a\n"
        "             file fails to compile\n"
        " -t count  - (only provide once) allocate <count> 4-KiB blocks for\n"
        "             the tape. (defaults to 8 if not specified)\n"
        " -e ext    - (only provide once) use 'ext' as the extension for\n"
        "             source files instead of '.bf'\n"
        "             (This program will remove this at the end of the input\n"
        "             file to create the output file name)\n"
        " -a arch   - compile for the specified architecture\n"
        "             (defaults to " BFC_DEFAULT_ARCH_STR
        " if not specified)**\n"
        " -s ext    - (only provide once) use 'ext' as the extension for\n"
        "             compiled binaries (empty if not specified)\n"
        " -A        - list supported architectures and exit\n"
        "\n"
        "* -q and -j will not affect arguments passed before they were.\n"
        "\n"
        "** Optimization can make error reporting less precise.\n"
        "\n"
        "Remaining options are treated as source file names. If they don't\n"
        "end with '.bf' (or the extension specified with '-e'), the program\n"
        "will raise an error.\n",
        progname
    );
}

static noreturn nonnull_args inline void report_version(const char *progname) {
    printf(
        "%s: eambfc version " BFC_VERSION
        "\n\n"
        "Copyright (c) 2024 Eli Array Minkoff.\n"
        "License: GNU GPL version 3 "
        "<https://gnu.org/licenses/gpl.html>.\n"
        "This is free software: "
        "you are free to change and redistribute it.\n"
        "There is NO WARRANTY, to the extent permitted by law.\n\n"
        "Build information:\n"
        " * " BFC_COMMIT "\n", /* git info or message stating git not used. */
        progname
    );
    exit(EXIT_SUCCESS);
}

static noreturn nonnull_args inline void list_arches(const char *progname) {
    printf(
        "This build of %s supports the following architectures:\n\n"
/* __BACKENDS__ add backend and any aliases in a block here*/
#if BFC_TARGET_X86_64
        "- x86_64 (aliases: x64, amd64, x86-64)\n"
#endif /* BFC_TARGET_X86_64 */
#if BFC_TARGET_ARM64
        "- arm64 (aliases: aarch64)\n"
#endif /* BFC_TARGET_ARM64 */
#if BFC_TARGET_RISCV64
        "- riscv64 (aliases: riscv)\n"
#endif /* BFC_TARGET_RISCV64 */
#if BFC_TARGET_S390X
        "- s390x (aliases: s390, s390x, z/architecture)\n"
#endif /* BFC_TARGET_S390X */

        "\nIf no architecture is specified, it defaults "
        "to " BFC_DEFAULT_ARCH_STR " .\n",
        progname
    );
    exit(EXIT_SUCCESS);
}

/* macro for use in parse_args function only.
 * SHOW_HINT:
 *  * unless -q or -j was passed, write the help text to stderr. */
#define SHOW_HINT() \
    if (!(rc.quiet || rc.json)) show_help(stderr, argc ? argv[0] : "eambfc")

run_cfg parse_args(int argc, char *argv[]) {
    int opt;
    char char_str_buf[2] = "";
    run_cfg rc = {
        .inter = NULL,
        .ext = NULL,
        .out_ext = NULL,
        .tape_blocks = 0,
        .quiet = false,
        .optimize = false,
        .keep = false,
        .moveahead = false,
        .json = false,
    };

    while ((opt = getopt(argc, argv, ":hVqjOkmAa:e:t:s:")) != -1) {
        switch (opt) {
        case 'h': show_help(stdout, argv[0]); exit(EXIT_SUCCESS);
        case 'V': report_version(argc ? argv[0] : "eambfc");
        case 'A': list_arches(argc ? argv[0] : "eambfc");
        case 'q':
            rc.quiet = true;
            quiet_mode();
            break;
        case 'j':
            rc.json = true;
            json_mode();
            break;
        case 'O': rc.optimize = true; break;
        case 'k': rc.keep = true; break;
        case 'm': rc.moveahead = true; break;
        case 'e':
            /* Print an error if ext was already set. */
            if (rc.ext != NULL) {
                basic_err("MULTIPLE_EXTENSIONS", "passed -e multiple times.");
                SHOW_HINT();
                exit(EXIT_FAILURE);
            }
            rc.ext = optarg;
            break;
        case 's':
            /* Print an error if out_ext was already set. */
            if (rc.out_ext != NULL) {
                basic_err(
                    "MULTIPLE_OUTPUT_EXTENSIONS", "passed -s multiple times."
                );
                SHOW_HINT();
                exit(EXIT_FAILURE);
            }
            rc.out_ext = optarg;
            break;
        case 't':
            /* Print an error if tape_blocks has already been set */
            if (rc.tape_blocks != 0) {
                basic_err(
                    "MULTIPLE_TAPE_BLOCK_COUNTS", "passed -t multiple times."
                );
                SHOW_HINT();
                exit(EXIT_FAILURE);
            }
            char *endptr;
            /* casting unsigned long long instead of using scanf as scanf can
             * lead to undefined behavior if input isn't well-crafted, and
             * unsigned long long is guaranteed to be at least 64 bits. */
            unsigned long long int holder = strtoull(optarg, &endptr, 10);
            /* if the full opt_arg wasn't consumed, it's not a numeric value. */
            if (*endptr != '\0') {
                param_err(
                    "NOT_NUMERIC",
                    "{} could not be parsed as a numeric value",
                    optarg
                );
                SHOW_HINT();
                exit(EXIT_FAILURE);
            }
            if (holder == 0) {
                basic_err("NO_TAPE", "Tape value for -t must be at least 1");
                SHOW_HINT();
                exit(EXIT_FAILURE);
            }
            /* if it's any larger than this, the tape size would exceed the
             * 64-bit integer limit. */
            if (holder >= (UINT64_MAX >> 12)) {
                param_err(
                    "TAPE_TOO_LARGE",
                    "{} * 0x1000 exceeds the 64-bit integer limit.",
                    optarg
                );
                SHOW_HINT();
                exit(EXIT_FAILURE);
            }
            rc.tape_blocks = (u64)holder;
            break;
        case 'a':
            if (rc.inter != NULL) {
                basic_err("MULTIPLE_ARCHES", "passed -a multiple times.");
                SHOW_HINT();
                exit(EXIT_FAILURE);
            }
            if (!select_inter(optarg, &rc.inter)) {
                SHOW_HINT();
                exit(EXIT_FAILURE);
            }
            break;
        case ':': /* one of -a, -e, or -t is missing an argument */
            char_str_buf[0] = (char)optopt;
            param_err(
                "MISSING_OPERAND",
                "{} requires an additional argument",
                char_str_buf
            );
            exit(EXIT_FAILURE);
        case '?': /* unknown argument */
            char_str_buf[0] = (char)optopt;
            param_err("UNKNOWN_ARG", "Unknown argument: {}.", char_str_buf);
            exit(EXIT_FAILURE);
        }
    }
    if (optind == argc) {
        basic_err("NO_SOURCE_FILES", "No source files provided.");
        SHOW_HINT();
        exit(EXIT_FAILURE);
    }

    /* if no extension was provided, use .bf */
    if (rc.ext == NULL) rc.ext = ".bf";

    /* if no tape size was specified, default to 8. */
    if (rc.tape_blocks == 0) rc.tape_blocks = 8;

    /* if no architecture was specified, default to default value set above */
    if (rc.inter == NULL) rc.inter = &BFC_DEFAULT_INTER;
    return rc;
}

#undef SHOW_HINT
