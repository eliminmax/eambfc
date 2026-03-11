/* SPDX-FileCopyrightText: 2025 - 2026 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

/* C99 */
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <unistd.h>
/* internal */
#include "arch_inter.h"
#include "attributes.h"
#include "config.h"
#include "err.h"
#include "setup.h"
#include "types.h"
#include "util.h"
#include "version.h"

#define HELP_TEMPLATE \
    "Usage: %s [options] <program.bf> [<program2.bf> ...]\n" \
    "\n" \
    " --help,         -h:   display this help text and exit\n" \
    " --version,      -V:   print version information and exit\n" \
    " --json,         -j:   print errors in JSON format " \
    "(conflicts with --quiet)\n" \
    " --quiet,        -q:   don't print any errors (conflicts with --json)\n" \
    " --optimize,     -O:   enable optimization " \
    "(can make error reporting less precise)\n" \
    " --continue,     -c:   continue to the next file on failure\n" \
    " --list-targets, -A:   list supported targets and exit\n" \
    " --keep-failed,  -k:   keep files that failed to compile\n" \
    "                 --:   stop argument parsing, treating " \
    "remaining arguments as filenames\n" \
    "\n" \
    "PARAMETER OPTIONS (provide at most once each):\n" \
    " --tape-size=count,      -t count:   " \
    "use <count> 4-KiB blocks for the tape (defaults to 8)\n" \
    " --source-extension=ext, -e ext:     " \
    "use 'ext' as the source extension (defaults to \"bf\")\n" \
    " --target-arch=arch,     -a arch:    " \
    "compile for the specified architecture (defaults " \
    "to " BFC_DEFAULT_ARCH_STR \
    ")\n" \
    " --output-extension=ext, -s ext:     " \
    "use 'ext' as the output extension (no extension if unset)\n"

/* Checks if `arg` matches any of `values`
 *
 * Returns `values[0]` if `strcmp` returns 0 when comparing `arg` t a non-null
 * pointer in `values`, and `NULL` if no match was found before reaching a null
 * pointer.
 *
 * Normal safety concerns around `strcmp` apply, and `values` must end with a
 * null pointer. */
nonnull_args static const char *any_match(
    const char *arg, const char *values[]
) {
    for (int i = 0; values[i]; i++) {
        if (strcmp(arg, values[i]) == 0) return values[0];
    }
    return NULL;
}

nonnull_args void report_version(const char *progname) {
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
}

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

void list_targets(void) {
#define ARCH_INTER(inter, name, ...) "- " name " (aliases: " #__VA_ARGS__ ")\n"

    puts(
        ARCH_LIST_START
#include "backends.h"
            ARCH_LIST_END
    );
}

#undef ARCH_LIST_START
#undef ARCH_LIST_END

typedef union {
    int raw;

    enum ok_outcome {
        OO_ADVANCE = -3,
        OO_COLLECT_REMAINING = -2,
        OO_SHORT_CIRCUIT = -1,
        OO_CONTINUE = 0,
    } ok;

    ArgParseOutcome err;
} OptionOutcome;

static nonnull_args OptionOutcome
set_tape_size(const char *operand, ArgParseOut *out) {
    if (!isdigit(*operand)) {
        free(out->ok.files);
        return (OptionOutcome){.err = ARGS_ERR_TAPE_SIZE_NOT_NUMERIC};
    }
    char *endptr;
    errno = 0;
    umax val = strtoumax(operand, &endptr, 10);
    if (errno == ERANGE || val > UINT64_MAX) {
        free(out->ok.files);
        out->err.str = operand;
        return (OptionOutcome){.err = ARGS_ERR_TAPE_SIZE_OVERFLOW};
    }
    if (!val) { return (OptionOutcome){.err = ARGS_ERR_TAPE_SIZE_ZERO}; }
    if (*endptr != '\0') {
        free(out->ok.files);
        out->err.str = operand;
        return (OptionOutcome){.err = ARGS_ERR_TAPE_SIZE_NOT_NUMERIC};
    }
    u64 old_val = out->ok.tape_blocks;
    if (old_val) {
        free(out->ok.files);
        out->err.tape_sizes[0] = old_val;
        out->err.tape_sizes[1] = val;
        return (OptionOutcome){.err = ARGS_ERR_MULTIPLE_TAPE_SIZES};
    }
    out->ok.tape_blocks = val;
    return (OptionOutcome){.ok = OO_CONTINUE};
}

static nonnull_args OptionOutcome
set_source_extension(const char *operand, ArgParseOut *out) {
    const char *old_ext = out->ok.source_extension;
    if (old_ext) {
        free(out->ok.files);
        out->err.str2[0] = old_ext;
        out->err.str2[1] = operand;
        return (OptionOutcome){.err = ARGS_ERR_MULTIPLE_SOURCE_EXTENSIONS};
    }
    return (OptionOutcome){.ok = OO_CONTINUE};
}

static nonnull_args OptionOutcome
set_output_extension(const char *operand, ArgParseOut *out) {
    const char *old_ext = out->ok.output_extension;
    if (old_ext) {
        free(out->ok.files);
        out->err.str2[0] = old_ext;
        out->err.str2[1] = operand;
        return (OptionOutcome){.err = ARGS_ERR_MULTIPLE_OUTPUT_EXTENSIONS};
    }
    return (OptionOutcome){.ok = OO_CONTINUE};
}

static nonnull_args OptionOutcome
set_backend(const char *operand, ArgParseOut *out) {
#define ARCH_INTER(target_inter, ...) \
    if (any_match(operand, (const char *[]){__VA_ARGS__, NULL})) { \
        out->ok.backend = &target_inter; \
        return (OptionOutcome){.ok = OO_CONTINUE}; \
    }
#define ARCH_INTER_DISABLED(name, /* aliases: */...) \
    if (any_match(operand, (const char *[]){name, __VA_ARGS__, NULL})) { \
        free(out->ok.files); \
        out->err.str = name; \
        return (OptionOutcome){.err = ARGS_ERR_DISABLED_BACKEND}; \
    }
#include "backends.h"

    free(out->ok.files);
    out->err.str = operand;
    return (OptionOutcome){.err = ARGS_ERR_UNKNOWN_BACKEND};
}

// return -2 for standalone --
// return -1 for --help, --version, or --list-targets
// set up out->err and return the error value if an error occurs
// return 0 if arg parsing should continue
static nonnull_args OptionOutcome longopt(char **args, ArgParseOut *out) {
    char *arg = &args[0][2];
    if (!arg[0]) return (OptionOutcome){.ok = OO_COLLECT_REMAINING};
    char *operand = NULL;
    char *eq;
    if ((eq = strchr(arg, '='))) {
        *eq = '\0';
        operand = eq + 1;
    }

#define REQUIRE_NO_OPERAND() \
    if (operand) { \
        free(out->ok.files); \
        out->err.str2[0] = arg; \
        out->err.str2[1] = operand; \
        return (OptionOutcome){.err = ARGS_ERR_UNEXPECTED_OPERAND}; \
    }

#define SHORT_CIRCUIT(OPT, RET) \
    if (strcmp(arg, OPT) == 0) { \
        REQUIRE_NO_OPERAND(); \
        out->ok.run_type = RET; \
        return (OptionOutcome){.ok = OO_SHORT_CIRCUIT}; \
    }
#define FLAG_OPTION(FLAG, FIELD_SET) \
    if (strcmp(arg, FLAG) == 0) { \
        REQUIRE_NO_OPERAND(); \
        out->ok.FIELD_SET; \
        return (OptionOutcome){.ok = OO_CONTINUE}; \
    }
#define PARAM_OPTION(OPT, FUNC) \
    if (strcmp(arg, OPT) == 0) { \
        if (!operand && !(operand = args[1])) { \
            free(out->ok.files); \
            out->err.str = arg; \
            return (OptionOutcome){.err = ARGS_ERR_MISSING_OPERAND}; \
        } \
        OptionOutcome outcome = FUNC(operand, out); \
        if (outcome.raw) return outcome; \
        return (OptionOutcome){.ok = eq ? OO_CONTINUE : OO_ADVANCE}; \
    }

    SHORT_CIRCUIT("help", SHOW_HELP);
    SHORT_CIRCUIT("version", SHOW_VERSION);
    SHORT_CIRCUIT("list-targets", LIST_TARGETS);
    FLAG_OPTION("json", out_mode |= OUTMODE_JSON);
    FLAG_OPTION("quiet", out_mode |= OUTMODE_QUIET);
    FLAG_OPTION("optimize", optimize = true);
    FLAG_OPTION("keep", keep = true);
    FLAG_OPTION("keep-failed", keep = true);
    FLAG_OPTION("continue", continue_on_error = true);
    PARAM_OPTION("tape_size", set_tape_size);
    PARAM_OPTION("source-extension", set_source_extension);
    PARAM_OPTION("target-arch", set_backend);
    PARAM_OPTION("output-extension", set_output_extension);
#undef FLAG_OPTION
#undef SHORT_CIRCUIT
#undef PARAM_OPTION
    free(out->ok.files);
    out->err.str = arg;
    return (OptionOutcome){.err = ARGS_ERR_UNKNOWN_LONG_OPT};
}

static nonnull_args OptionOutcome
shortopts(char *const *args, ArgParseOut *out) {
    static char MISSING_ERR[3] = "-";
    const char *arg = &args[0][1];
    if (!*arg) {
        free(out->ok.files);
        return (OptionOutcome){.err = ARGS_ERR_SINGLE_DASH_ARG};
    }

    const char *operand;

    OptionOutcome (*set_fn)(const char *, ArgParseOut *);

    while (arg) {
        switch (*arg) {
            case 'h':
                out->ok.run_type = SHOW_HELP;
                return (OptionOutcome){.ok = OO_SHORT_CIRCUIT};
            case 'V':
                out->ok.run_type = SHOW_VERSION;
                return (OptionOutcome){.ok = OO_SHORT_CIRCUIT};
            case 'A':
                out->ok.run_type = LIST_TARGETS;
                return (OptionOutcome){.ok = OO_SHORT_CIRCUIT};
            case 'j':
                out->ok.out_mode |= OUTMODE_JSON;
                break;
            case 'q':
                out->ok.out_mode |= OUTMODE_QUIET;
                break;
            case 'O':
                out->ok.optimize = true;
                break;
            case 'k':
                out->ok.keep = true;
                break;
            case 'c':
                out->ok.continue_on_error = true;
                break;
            case 't':
                set_fn = set_tape_size;
                goto operand_fn;
            case 'e':
                set_fn = set_source_extension;
                goto operand_fn;
            case 's':
                set_fn = set_output_extension;
                goto operand_fn;
            case 'a':
                set_fn = set_backend;
                goto operand_fn;
            default:
                free(out->ok.files);
                out->err.unknown_short_opt = *arg;
                return (OptionOutcome){.err = ARGS_ERR_UNKNOWN_SHORT_OPT};
        }
        ++arg;
    }
    return (OptionOutcome){.ok = OO_CONTINUE};

operand_fn:
    operand = arg[1] ? arg + 1 : args[1];
    if (!operand) {
        MISSING_ERR[1] = *arg;
        free(out->ok.files);
        out->err.str = MISSING_ERR;
        return (OptionOutcome){.err = ARGS_ERR_MISSING_OPERAND};
    }
    OptionOutcome outcome = set_fn(operand, out);
    if (outcome.raw) return outcome;
    // if arg[1] is 0, then the next parameter will have been used up
    return (OptionOutcome){.ok = arg[1] ? OO_CONTINUE : OO_ADVANCE};
}

nonnull_args ArgParseOutcome
parse_args(int argc, char *argv[], ArgParseOut *out) {
    char **files = checked_malloc(argc + 1);
    for (int i = 1; i <= argc; ++i) files[i] = NULL;

    // set all fields to default values
    out->ok = (RunConfig){0};

    for (int i = 0; i < argc; ++i) {
        if (argv[i][0] == '-') {
            OptionOutcome res = (argv[i][1] == '-') ? longopt(&argv[i], out) :
                                                      shortopts(&argv[i], out);
            switch (res.raw) {
                case OO_COLLECT_REMAINING:
                    for (++i; i < argc; ++i) {
                        out->ok.files[out->ok.nfiles++] = argv[i];
                    }
                    break;
                case OO_SHORT_CIRCUIT:
                    free(out->ok.files);
                    return ARGS_OK;
                case OO_ADVANCE:
                    ++i;
                case OO_CONTINUE:
                    break;
                default:
                    return res.err;
            }
        } else {
            out->ok.files[out->ok.nfiles++] = argv[i];
        }
    }
    if (out->ok.out_mode == (OUTMODE_JSON | OUTMODE_QUIET)) {
        free(out->ok.files);
        return ARGS_ERR_SET_BOTH_OUT_MODES;
    }

    if (!out->ok.source_extension) out->ok.source_extension = "bf";

    const char *o;

    if ((o = out->ok.output_extension) && o == out->ok.source_extension) {
        free(out->ok.files);
        out->err.str = o;
        return ARGS_ERR_INPUT_IS_OUTPUT;
    }

    if (!out->ok.nfiles) {
        free(out->ok.files);
        return ARGS_ERR_NO_SOURCE_FILES;
    }

    if (!out->ok.backend) { out->ok.backend = &BFC_DEFAULT_INTER; }

    u64 max_tape_blocks;
    if (out->ok.backend->addr_class == PTRSIZE_32) {
        max_tape_blocks = UINT32_MAX / 0x1000;
    } else {
        max_tape_blocks = UINT64_MAX / 0x1000;
    }
    if (out->ok.tape_blocks > max_tape_blocks) {
        free(out->ok.files);
        u64 tape_blocks = out->ok.tape_blocks;
        int bits = 32 * (out->ok.backend->addr_class);

        out->err.tape_too_large = (struct tape_too_large){tape_blocks, bits};
        return ARGS_ERR_TAPE_TOO_LARGE;
    }
    return ARGS_OK;
}
