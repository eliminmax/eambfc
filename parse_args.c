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
#include "config.h"
#include "err.h"
#include "parse_args.h"
#include "version.h"

#if BFC_GNU_ARGS
/* GNU C */
#include <getopt.h>
const struct option longopts[] = {
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, 'V'},
    {"quiet", no_argument, 0, 'q'},
    {"json", no_argument, 0, 'j'},
    {"optimize", no_argument, 0, 'O'},
    {"keep-failed", no_argument, 0, 'k'},
    {"continue", no_argument, 0, 'c'},
    {"list-targets", no_argument, 0, 'A'},
    {"target-arch", required_argument, 0, 'a'},
    {"tape-size", required_argument, 0, 't'},
    {"source-suffix", required_argument, 0, 'e'},
    {"output-suffix", required_argument, 0, 's'},
    {0, 0, 0, 0},
};

/* ignored but need a non-null pointer to pass to getopt_long */
static int arg_index = 0;
/* use this macro in place of normal getopt */
#define getopt(c, v, opts) getopt_long(c, v, opts, longopts, &arg_index)
#define OPTION(l, s, pad, msg) " --" l ", " pad "-" s ":   " msg
#define PARAM_OPT(l, s, a, spad, lpad, msg) \
    " --" l "=" a ", " lpad " -" s " " a ":    " spad msg
#else
#define OPTION(l, s, pad, msg) " -" s "    " msg
#define PARAM_OPT(l, s, a, spad, lpad, msg) " -" s " " a spad "    " msg
#endif

/* this macro hell defines the help template. If using GNU longopts, pads with
 * the provided padding string to keep descriptions even, and for the parameter
 * options, uses the second padding string to pad out the parameter argument. */
#define OPTION_HELP \
    OPTION("help", "h", "        ", "display this help text and exit")
#define OPTION_VERSION \
    OPTION("version", "V", "     ", "print version information and exit")
#define OPTION_JSON \
    OPTION("json", "j", "        ", "print errors in JSON format*")
#define OPTION_QUIET OPTION("quiet", "q", "       ", "don't print any errors*")
#define OPTION_OPTIMIZE OPTION("optimize", "O", "    ", "enable optimization**")
#define OPTION_MOVEAHEAD \
    OPTION("continue", "c", "    ", "continue to the next file on failure")
#define OPTION_LIST_TARGETS \
    OPTION("list-targets", "A", "", "list supported targets and exit")
#define OPTION_KEEP_FAILED \
    OPTION("keep-failed", "k", " ", "keep files that failed to compile")
#define OPTIONS \
    OPTION_HELP "\n" OPTION_VERSION "\n" OPTION_JSON "\n" OPTION_QUIET \
                "\n" OPTION_OPTIMIZE "\n" OPTION_MOVEAHEAD \
                "\n" OPTION_LIST_TARGETS "\n" OPTION_KEEP_FAILED "\n"

#define PARAM_OPT_TAPE_SIZE \
    PARAM_OPT( \
        "tape-size", \
        "t", \
        "count", \
        "", \
        "     ", \
        "use <count> 4-KiB blocks for the tape" \
    )
#define PARAM_OPT_SRC_EXT \
    PARAM_OPT( \
        "source-extension", \
        "e", \
        "ext", \
        "  ", \
        "", \
        "use 'ext' as the source extension" \
    )
#define PARAM_OPT_TARGET_ARCH \
    PARAM_OPT( \
        "target-arch", \
        "a", \
        "arch", \
        " ", \
        "    ", \
        "compile for the specified architecture" \
    )
#define PARAM_OPT_OUT_SUFFIX \
    PARAM_OPT( \
        "output-suffix", \
        "s", \
        "suf", \
        "  ", \
        "   ", \
        "append 'suf' to output file names" \
    )
#define PARAM_OPTS \
    PARAM_OPT_TAPE_SIZE "\n" PARAM_OPT_SRC_EXT "\n" PARAM_OPT_TARGET_ARCH \
                        "\n" PARAM_OPT_OUT_SUFFIX "\n"

#define HELP_TEMPLATE \
    "Usage: %s [options] <program.bf> [<program2.bf> ...]\n" \
    "\n" \
    "" OPTIONS \
    "\n* -q and -j will not affect arguments passed before they were.\n" \
    "\n" \
    "** Optimization can make error reporting less precise.\n" \
    "\n" \
    "PARAMETER OPTIONS (provide at most once each)\n" \
    "" PARAM_OPTS \
    "\n" \
    "If not provided, it falls back to 8 as the tape-size count, \".bf\" " \
    "as the source extension, " BFC_DEFAULT_ARCH_STR \
    " as the target-arch, and an empty output-suffix.\n" \
    "\n" \
    "Remaining options are treated as source file names. If they don't " \
    "end with the right extension, the program will raise an error.\n" \
    "Additionally, passing \"--\" as a standalone argument will stop " \
    "argument parsing, and treat remaining arguments as source file names.\n"

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
    char unknown_msg[64];
    if (sprintf(unknown_msg, "%32s", arch_arg) == 32 && unknown_msg[31]) {
        strcat(unknown_msg, "...");
    }
    strcat(unknown_msg, " is not a recognized target");
    display_err((bf_comp_err){.file = NULL,
                              .has_instr = false,
                              .has_location = false,
                              .id = BF_ERR_UNKNOWN_ARCH,
                              .msg = unknown_msg});
    return false;
}

static noreturn nonnull_args void report_version(const char *progname) {
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

static noreturn nonnull_args void list_arches(const char *progname) {
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
        "- s390x (aliases: s390, z/architecture)\n"
#endif /* BFC_TARGET_S390X */

        "\nIf no architecture is specified, it defaults "
        "to " BFC_DEFAULT_ARCH_STR ".\n",
        progname
    );
    exit(EXIT_SUCCESS);
}

/* macros for use in parse_args function only.
 * PROGNAME:
 *  * use argv[0], but fall back to "eambfc" if argc == 0 or if argv[0] is NULL
 * SHOW_HINT:
 *  * unless -q or -j was passed, write the help text to stderr. */
#define PROGNAME (argc && argv[0] != NULL) ? argv[0] : "eambfc"
#define SHOW_HINT() \
    do { \
        if (!(rc.quiet || rc.json)) fprintf(stderr, HELP_TEMPLATE, PROGNAME); \
        exit(EXIT_FAILURE); \
    } while (false)

run_cfg parse_args(int argc, char *argv[]) {
    int opt;
    char missing_op_msg[34] = "% requires an additional argument";
    char unknown_arg_msg[20] = "Unknown argument: %";
    run_cfg rc = {
        .inter = NULL,
        .ext = NULL,
        .out_ext = NULL,
        .tape_blocks = 0,
        .quiet = false,
        .optimize = false,
        .keep = false,
        .cont_on_fail = false,
        .json = false,
    };

    while ((opt = getopt(argc, argv, ":hVqjOkmcAa:e:t:s:")) != -1) {
        switch (opt) {
        case 'h': printf(HELP_TEMPLATE, PROGNAME); exit(EXIT_SUCCESS);
        case 'V': report_version(PROGNAME);
        case 'A': list_arches(PROGNAME);
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
        case 'm':
        case 'c': rc.cont_on_fail = true; break;
        case 'e':
            /* Print an error if ext was already set. */
            if (rc.ext != NULL) {
                basic_err(
                    BF_ERR_MULTIPLE_EXTENSIONS, "passed -e multiple times."
                );
                SHOW_HINT();
            }
            rc.ext = optarg;
            break;
        case 's':
            /* Print an error if out_ext was already set. */
            if (rc.out_ext != NULL) {
                basic_err(
                    BF_ERR_MULTIPLE_OUTPUT_EXTENSIONS,
                    "passed -s multiple times."
                );
                SHOW_HINT();
            }
            rc.out_ext = optarg;
            break;
        case 't':
            /* Print an error if tape_blocks has already been set */
            if (rc.tape_blocks != 0) {
                basic_err(
                    BF_ERR_MULTIPLE_TAPE_BLOCK_COUNTS,
                    "passed -t multiple times."
                );
                SHOW_HINT();
            }
            char *endptr;
            /* casting unsigned long long instead of using scanf as scanf can
             * lead to undefined behavior if input isn't well-crafted, and
             * unsigned long long is guaranteed to be at least 64 bits. */
            unsigned long long int holder = strtoull(optarg, &endptr, 10);
            /* if the full opt_arg wasn't consumed, it's not a numeric value. */
            if (*endptr != '\0') {
                basic_err(
                    BF_ERR_TAPE_SIZE_NOT_NUMERIC,
                    "tape size could not be parsed as a numeric value"
                );
                SHOW_HINT();
            }
            if (holder == 0) {
                basic_err(
                    BF_ERR_TAPE_SIZE_ZERO,
                    "Tape value for -t must be at least 1"
                );
                SHOW_HINT();
            }
            /* if it's any larger than this, the tape size would exceed the
             * 64-bit integer limit. */
            if (holder >= (UINT64_MAX >> 12)) {
                basic_err(
                    BF_ERR_TAPE_TOO_LARGE,
                    "tape size too large to avoid overflow"
                );
                SHOW_HINT();
            }
            rc.tape_blocks = (u64)holder;
            break;
        case 'a':
            if (rc.inter != NULL) {
                basic_err(BF_ERR_MULTIPLE_ARCHES, "passed -a multiple times.");
                SHOW_HINT();
            }
            if (!select_inter(optarg, &rc.inter)) { SHOW_HINT(); }
            break;
        case ':': /* one of -a, -e, or -t is missing an argument */
            missing_op_msg[0] = optopt;
            basic_err(BF_ERR_MISSING_OPERAND, missing_op_msg);
            SHOW_HINT();
        case '?': /* unknown argument */
            unknown_arg_msg[18] = optopt;
            basic_err(BF_ERR_UNKNOWN_ARG, unknown_arg_msg);
            SHOW_HINT();
        }
    }
    if (optind == argc) {
        basic_err(BF_ERR_NO_SOURCE_FILES, "No source files provided.");
        SHOW_HINT();
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
#undef PROGNAME
