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
#include <attributes.h>
#include <config.h>

#include "arch_inter.h"
#include "err.h"
#include "setup.h"
#include "version.h"

#if BFC_LONGOPTS
#include "util.h"
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

/* use this macro in place of normal getopt */
#define GETOPT_FN(c, v, opts) getopt_long(c, v, opts, longopts, NULL)
#define OPTION(l, s, pad, msg) " --" l ", " pad "-" s ":   " msg
#define PARAM_OPT(l, s, a, spad, lpad, msg) \
    " --" l "=" a ", " lpad " -" s " " a ":    " spad msg
#else
#define OPTION(l, s, pad, msg) " -" s ":   " msg
#define PARAM_OPT(l, s, a, spad, lpad, msg) " -" s " " spad a ":   " msg
#define GETOPT_FN(c, v, opts) getopt(c, v, opts)
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
    "PARAMETER OPTIONS (provide at most once each):\n" \
    "" PARAM_OPTS \
    "\n" \
    "If not provided, it falls back to 8 as the tape-size count, \".bf\" " \
    "as the source extension, " BFC_DEFAULT_ARCH_STR \
    " as the target-arch, and an empty output-suffix.\n" \
    "\n" \
    "Remaining options are treated as source file names. If they don't " \
    "end with the right extension, the program will raise an error.\n" \
    "\n" \
    "Additionally, passing \"--\" as a standalone argument will stop " \
    "argument parsing, and treat remaining arguments as source file names.\n"

/* checks if `arg` matches any of `values`
 *
 * Returns `true` if `strcmp` returns 0 when comparing `arg` t a non-null
 * pointer in `values`, and `false` if no match was found before reaching a null
 * pointer.
 *
 * normal safety concerns around `strcmp` apply, and `values` must end with a
 * null pointer */
nonnull_args static bool any_match(const char *arg, const char *values[]) {
    for (int i = 0; values[i]; i++) {
        if (strcmp(arg, values[i]) == 0) return true;
    }
    return false;
}

nonnull_args static bool select_inter(const char *arch_arg, ArchInter **inter) {
#define ARCH_INTER(target_inter, ...) \
    if (any_match(arch_arg, (const char *[]){__VA_ARGS__, NULL})) { \
        *inter = &target_inter; \
        return true; \
    }
#include "backends.h"

    char unknown_msg[64];
    if (sprintf(unknown_msg, "%32s", arch_arg) == 32 && unknown_msg[31]) {
        strcat(unknown_msg, "...");
    }
    strcat(unknown_msg, " is not a recognized target");
    display_err(basic_err(BF_ERR_UNKNOWN_ARCH, unknown_msg));
    return false;
#undef SET_IF_MATCHES
}

noreturn static nonnull_args void report_version(const char *progname) {
    /* strip leading path from progname */
    const char *filename;
    while ((filename = strchr(progname, '/'))) progname = filename + 1;

    printf(
        "%s%s version " BFC_VERSION
        "\n\n"
        "Copyright (c) 2024 - 2025 Eli Array Minkoff.\n"
        "License: GNU GPL version 3 "
        "<https://gnu.org/licenses/gpl.html>.\n"
        "This is free software: "
        "you are free to change and redistribute it.\n"
        "There is NO WARRANTY, to the extent permitted by law.\n\n"
        "Build information:\n"
        " * " BFC_COMMIT "\n", /* git info or message stating git not used. */
        progname,
        /* if progname is "eambfc", this will display "eambfc version...".
         * if it's anything else, it'll add ": eambfc" after the progname */
        strcmp(progname, "eambfc") ? ": eambfc" : ""
    );
    exit(EXIT_SUCCESS);
}

#include <config.h>
#if BFC_NUM_BACKENDS == 1
#define ARCH_LIST_START \
    "This build of eambfc only supports the following architecture:\n\n"
#define ARCH_LIST_END
#else /* BFC_NUM_BACKENDS */
#define ARCH_LIST_START \
    "This build of eambfc supports the following architectures:\n\n"
#define ARCH_LIST_END \
    "\nIf no architectures is specified, it defaults to " BFC_DEFAULT_ARCH_STR \
    "."
#endif /* BFC_NUM_BACKENDS */

noreturn static nonnull_args void list_arches(void) {
#define ARCH_INTER(inter, name, ...) "- " name " (aliases: " #__VA_ARGS__ ")\n"

    puts(ARCH_LIST_START
#include "backends.h"
             ARCH_LIST_END);
    exit(EXIT_SUCCESS);
}

#undef ARCH_LIST_START
#undef ARCH_LIST_END

noreturn static nonnull_args void bad_arg(
    const char *progname, BfErrorId id, const char *msg, bool show_hint
) {
    display_err(basic_err(id, msg));
    if (show_hint) fprintf(stderr, HELP_TEMPLATE, progname);
    exit(EXIT_FAILURE);
}

RunCfg process_args(int argc, char *argv[]) {
    int opt;
    char missing_op_msg[35] = "-% requires an additional argument";
    char unknown_arg_msg[24] = "Unknown argument: -%";
    bool show_hint = true;
    RunCfg rc = {
        .inter = NULL,
        .ext = NULL,
        .out_ext = NULL,
        .tape_blocks = 0,
        .optimize = false,
        .keep = false,
        .cont_on_fail = false,
    };

    const char *progname = (argc && argv[0] != NULL) ? argv[0] : "eambfc";

    while ((opt = GETOPT_FN(argc, argv, ":hVqjOkcAa:e:t:s:")) != -1) {
        switch (opt) {
            case 'h':
                printf(HELP_TEMPLATE, progname);
                exit(EXIT_SUCCESS);
            case 'V':
                report_version(progname);
            case 'A':
                list_arches();
            case 'q':
                show_hint = false;
                quiet_mode();
                break;
            case 'j':
                show_hint = false;
                json_mode();
                break;
            case 'O':
                rc.optimize = true;
                break;
            case 'k':
                rc.keep = true;
                break;
            case 'c':
                rc.cont_on_fail = true;
                break;
            case 'e':
                /* Print an error if ext was already set. */
                if (rc.ext != NULL) {
                    bad_arg(
                        progname,
                        BF_ERR_MULTIPLE_EXTENSIONS,
                        "passed -e multiple times.",
                        show_hint
                    );
                }
                rc.ext = optarg;
                break;
            case 's':
                /* Print an error if out_ext was already set. */
                if (rc.out_ext != NULL) {
                    bad_arg(
                        progname,
                        BF_ERR_MULTIPLE_OUTPUT_EXTENSIONS,
                        "passed -s multiple times.",
                        show_hint
                    );
                }
                rc.out_ext = optarg;
                break;
            case 't':
                /* Print an error if tape_blocks has already been set */
                if (rc.tape_blocks != 0) {
                    bad_arg(
                        progname,
                        BF_ERR_MULTIPLE_TAPE_BLOCK_COUNTS,
                        "passed -t multiple times.",
                        show_hint
                    );
                }
                char *endptr;
                /* casting unsigned long long instead of using scanf as scanf
                 * can lead to undefined behavior if input isn't well-crafted,
                 * and unsigned long long is guaranteed to be at least 64 bits.
                 */
                unsigned long long int holder = strtoull(optarg, &endptr, 10);
                /* if the full opt_arg wasn't consumed, it's not a numeric
                 * value. */
                if (*endptr != '\0') {
                    bad_arg(
                        progname,
                        BF_ERR_TAPE_SIZE_NOT_NUMERIC,
                        "tape size could not be parsed as a numeric value",
                        show_hint
                    );
                }
                if (holder == 0) {
                    bad_arg(
                        progname,
                        BF_ERR_TAPE_SIZE_ZERO,
                        "Tape value for -t must be at least 1",
                        show_hint
                    );
                }
                /* if it's any larger than this, the tape size would exceed the
                 * 64-bit integer limit. */
                if (holder >= (UINT64_MAX >> 12)) {
                    bad_arg(
                        progname,
                        BF_ERR_TAPE_TOO_LARGE,
                        "tape size too large to avoid overflow",
                        show_hint
                    );
                }
                rc.tape_blocks = (u64)holder;
                break;
            case 'a':
                if (rc.inter != NULL) {
                    bad_arg(
                        progname,
                        BF_ERR_MULTIPLE_ARCHES,
                        "passed -a multiple times.",
                        show_hint
                    );
                }
                if (!select_inter(optarg, &rc.inter)) { exit(EXIT_FAILURE); }
                break;
            case ':': /* one of -a, -e, or -t is missing an argument */
                missing_op_msg[1] = optopt;
                bad_arg(
                    progname, BF_ERR_MISSING_OPERAND, missing_op_msg, show_hint
                );
            case '?': /* unknown argument */
#if BFC_LONGOPTS
                if (!optopt) {
                    int badarg = optind - 1;
                    char *msg = checked_malloc(strlen(argv[badarg]) + 19);
                    memcpy(msg, unknown_arg_msg, 18);
                    strcpy(&msg[18], argv[badarg]);
                    /* can't just use bad_arg as msg won't be freed. */
                    display_err((BFCError){
                        .id = BF_ERR_UNKNOWN_ARG,
                        .msg.alloc = msg,
                        .is_alloc = true,
                    });
                    if (show_hint) fprintf(stderr, HELP_TEMPLATE, progname);
                    exit(EXIT_FAILURE);
                }
#endif /* BFC_LONGOPTS */
                if (optopt >= 040 && optopt < 0x80) {
                    unknown_arg_msg[19] = optopt;
                } else {
                    sprintf(&unknown_arg_msg[19], "\\x%02hhx", (uchar)optopt);
                }
                bad_arg(
                    progname, BF_ERR_UNKNOWN_ARG, unknown_arg_msg, show_hint
                );
        }
    }

    /* if no extension was provided, use .bf */
    if (rc.ext == NULL) rc.ext = ".bf";

    if (rc.out_ext != NULL && strcmp(rc.out_ext, rc.ext) == 0) {
        display_err(basic_err(
            BF_ERR_INPUT_IS_OUTPUT,
            "Extension can't be the same as output suffix"
        ));
        exit(EXIT_FAILURE);
    }

    if (optind == argc) {
        bad_arg(
            progname,
            BF_ERR_NO_SOURCE_FILES,
            "No source files provided.",
            show_hint
        );
    }

    /* if no tape size was specified, default to 8. */
    if (rc.tape_blocks == 0) rc.tape_blocks = 8;

    /* if no architecture was specified, default to default value set above */
    if (rc.inter == NULL) rc.inter = &BFC_DEFAULT_INTER;
    return rc;
}
