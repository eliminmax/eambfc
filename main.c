/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A Brainfuck to x86_64 Linux ELF compiler. */

/* C99 */
#include <stdarg.h> /* va_* */
#include <stdio.h> /* FILE, stderr, stdout, printf, fprintf */
#include <stdlib.h> /* malloc, free, EXIT_*, strtoull */
#include <string.h> /* strncmp, strlen, strcpy */
/* POSIX */
#include <fcntl.h> /* O_*, mode_t */
#include <unistd.h> /* getopt, optopt */
/* internal */
#include "arch_inter.h" /* arch_inter, *_INTER */
#include "compile.h" /* bf_compile */
#include "config.h" /* BFC_DEFAULT_*, BFC_TARGET_* */
#include "err.h" /* *_err */
#include "resource_mgr.h" /* mgr_*, register_mgr */
#include "types.h" /* bool, u64, UINT64_MAX */
#include "version.h" /* BFC_VERSION, BFC_COMMIT */

/* print the help message to outfile. progname should be argv[0]. */
static void show_help(FILE *outfile, const char *progname) {
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

/* returns true if strcmp matches s to any strings in its argument,
 * and false otherwise.
 * normal safety concerns around strcmp apply. */
static bool any_match(const char *s, int count, ...) {
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

/* remove ext from end of str. If str doesn't end with ext, return false. */
static bool rm_ext(char *str, const char *ext) {
    size_t strsz = strlen(str);
    size_t extsz = strlen(ext);
    /* strsz must be at least 1 character longer than extsz to continue. */
    if (strsz <= extsz) return false;
    /* because of the above check, distance is known to be a positive value. */
    size_t distance = strsz - extsz;
    /* return 0 if str does not end in extsz*/
    if (strncmp(str + distance, ext, extsz) != 0) return false;
    /* set the beginning of the match to the null byte, to end str early */
    str[distance] = false;
    return true;
}

typedef struct {
    arch_inter *inter;
    char *ext;
    char *out_ext;
    u64 tape_blocks;
    /* use bitfield booleans here */
    bool quiet    : 1;
    bool optimize : 1;
    bool keep     : 1;
    bool moveahead: 1;
    bool json     : 1;
} run_cfg;

/* macro for use in parse_args function only.
 * SHOW_HINT:
 *  * unless -q or -j was passed, write the help text to stderr. */
#define SHOW_HINT() \
    if (!(rc.quiet || rc.json)) show_help(stderr, argv[0])

static run_cfg parse_args(int argc, char *argv[]) {
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
        case 'V':
            printf(
                "%s: eambfc version %s\n\n"
                "Copyright (c) 2024 Eli Array Minkoff.\n"
                "License: GNU GPL version 3 "
                "<https://gnu.org/licenses/gpl.html>.\n"
                "This is free software: "
                "you are free to change and redistribute it.\n"
                "There is NO WARRANTY, to the extent permitted by law.\n\n"
                "Build information:\n"
                " * %s\n", /* git info or message stating git not used. */
                argv[0],
                BFC_VERSION,
                BFC_COMMIT
            );
            exit(EXIT_SUCCESS);
        case 'A':
            printf(
                "This build of %s supports the following architectures:\n\n"
/* __BACKENDS__ */
#if BFC_TARGET_X86_64
                "- x86_64 (aliases: x64, amd64, x86-64)\n"
#endif /* BFC_TARGET_X86_64 */
#if BFC_TARGET_ARM64
                "- arm64 (aliases: aarch64)\n"
#endif /* BFC_TARGET_ARM64 */
#if BFC_TARGET_S390X
                "- s390x (aliases: s390, s390x, z/architecture)\n"
#endif /* BFC_TARGET_S390X */

                "\nIf no architecture is specified, it defaults "
                "to " BFC_DEFAULT_ARCH_STR " .\n",
                argv[0]
            );
            exit(EXIT_SUCCESS);

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
            /* either a bunch of #if preprocessor stuff or this, and the former
             * would need to have an `if (false)` to make sure it's valid.
             * Instead, use the macros and trust the compiler to optimize out
             * the constant check, and optimize out any disabled blocks. */
            /* __BACKENDS__ add a block here */
            if (BFC_TARGET_X86_64 &&
                any_match(optarg, 4, "x86_64", "x64", "amd64", "x86-64")) {
                rc.inter = &X86_64_INTER;
            } else if (BFC_TARGET_ARM64 &&
                       any_match(optarg, 2, "arm64", "aarch64")) {
                rc.inter = &ARM64_INTER;
            } else if (BFC_TARGET_S390X &&
                       any_match(
                           optarg, 3, "s390x", "s390", "z/architecture"
                       )) {
                rc.inter = &S390X_INTER;
            } else {
                param_err(
                    "UNKNOWN_ARCH",
                    "{} is not a recognized architecture",
                    optarg
                );
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

/* compile a file */
static bool compile_file(const char *filename, const run_cfg *rc) {
    char *outname = mgr_malloc(strlen(filename) + 1);
    strcpy(outname, filename);

    if (!rm_ext(outname, rc->ext)) {
        param_err(
            "BAD_EXTENSION",
            "File {} does not end with expected extension.",
            filename
        );
        mgr_free(outname);
        return false;
    }
    if (rc->out_ext != NULL) {
        size_t outname_sz = strlen(outname);
        outname = mgr_realloc(outname, outname_sz + strlen(rc->out_ext) + 1);
        strcat(outname, rc->out_ext);
    }

    int src_fd = mgr_open(filename, O_RDONLY);
    if (src_fd < 0) {
        param_err("OPEN_R_FAILED", "Failed to open {} for reading.", filename);
        mgr_free(outname);
        return false;
    }
    int dst_fd = mgr_open_m(outname, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (dst_fd < 0) {
        param_err("OPEN_W_FAILED", "Failed to open {} for writing.", outname);
        mgr_close(src_fd);
        mgr_free(outname);
        return false;
    }
    bool result =
        bf_compile(rc->inter, src_fd, dst_fd, rc->optimize, rc->tape_blocks);
    if ((!result) && (!rc->keep)) remove(outname);
    mgr_close(src_fd);
    mgr_close(dst_fd);
    mgr_free(outname);
    return result;
}

int main(int argc, char *argv[]) {
    /* register atexit function to clean up any open files or memory allocations
     * left behind. */
    if (!argc) {
        basic_err(
            "NO_CMDLINE_ARGS",
            "main called with argc=0, so something's wrong here"
        );
        return EXIT_FAILURE;
    }
#ifndef SKIP_RESOURCE_MGR
    register_mgr();
#endif /* SKIP_RESOURCE_MGR */
    int ret = EXIT_SUCCESS;
    run_cfg rc = parse_args(argc, argv);
    for (int i = optind; i < argc; i++) {
        if (compile_file(argv[i], &rc)) continue;
        ret = EXIT_FAILURE;
        if (!rc.moveahead) break;
    }

    return ret;
}
