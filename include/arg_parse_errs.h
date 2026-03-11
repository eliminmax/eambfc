/*
 * SPDX-FileCopyrightText: 2026 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef ARGS_ERR
#error macro "ARGS_ERR" should only be set in arg_parse_errs.h
#elif defined DEFINE_ARGS_ERR
#define ARGS_ERR(name, fmt, ...) ARGS_ERR_##name,
#elif defined PRINT_ARGS_ERR
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#define ARGS_ERR(name, fmt, ...) \
    case ARGS_ERR_##name: \
        fprintf(stderr, "Error: " fmt ".\n", __VA_ARGS__); \
        show_help(argv[0], stderr); \
        break;
#else
#define ARGS_ERR(err, fmt, ...)
#if __STDC_VERSION__ >= 202311L
#warning "Both PRINT_ARGS_ERR and DEFINE_ARGS_ERR are undefined"
#endif
#endif /* ARGS_ERR */

ARGS_ERR(DISABLED_BACKEND, "backend %s is disabled", parsed_args.err.str)
ARGS_ERR(
    DOT_IN_OUTPUT_EXTENSION,
    "output extension %s is invalid, as it contains '.'",
    parsed_args.err.str
)
ARGS_ERR(
    DOT_IN_SOURCE_EXTENSION,
    "source extension %s is invalid, as it contains '.'",
    parsed_args.err.str
)
ARGS_ERR(
    INPUT_IS_OUTPUT,
    "extension %s would be used for both source and output files",
    parsed_args.err.str
)
ARGS_ERR(
    MISSING_OPERAND, "argument %s requires an operand", parsed_args.err.str
)
ARGS_ERR(
    MULTIPLE_ARCHITECTURES,
    "provided multiple backends: %s and %s",
    parsed_args.err.str2[0],
    parsed_args.err.str2[1]
)
ARGS_ERR(
    MULTIPLE_SOURCE_EXTENSIONS,
    "provided multiple source file extensions: %s and %s",
    parsed_args.err.str2[0],
    parsed_args.err.str2[1]
)
ARGS_ERR(
    MULTIPLE_OUTPUT_EXTENSIONS,
    "provided multiple output file extensions: %s and %s",
    parsed_args.err.str2[0],
    parsed_args.err.str2[1]
)
ARGS_ERR(
    DUPLICATE_TAPE_SIZE,
    "tape size %ju provided multiple times",
    (umax)parsed_args.err.tape_sizes[0]
)
ARGS_ERR(
    MULTIPLE_TAPE_SIZES,
    "both %ju and %ju provided as tape size",
    (umax)parsed_args.err.tape_sizes[0],
    (umax)parsed_args.err.tape_sizes[1]
)
ARGS_ERR(NO_SOURCE_FILES, "no source files provided", 0)
ARGS_ERR(SET_BOTH_OUT_MODES, "attempted to set both quiet and json output", 0)
ARGS_ERR(
    SINGLE_DASH_ARG,
    "`-` as a standalone arg is not supported. "
    "Use `--` as a terminal argument instead",
    0
)
ARGS_ERR(
    TAPE_SIZE_NOT_NUMERIC,
    "non-numeric tape size %s provided",
    parsed_args.err.str
)
ARGS_ERR(
    TAPE_SIZE_OVERFLOW,
    "overflow parsing %s as a 64-bit unsigned int",
    parsed_args.err.str
)
ARGS_ERR(TAPE_SIZE_ZERO, "tape size cannot be zero pages", 0)
ARGS_ERR(
    TAPE_TOO_LARGE,
    "%ju 4KiB blocks can't fit in %d-bit address space",
    (umax)parsed_args.err.tape_too_large.tape_blocks,
    (int)parsed_args.err.tape_too_large.bits
)
ARGS_ERR(
    UNEXPECTED_OPERAND,
    "option %s was provided unexpected operand %s",
    parsed_args.err.str2[0],
    parsed_args.err.str2[1]
)
ARGS_ERR(UNKNOWN_BACKEND, "unknown backend: %s", parsed_args.err.str)
ARGS_ERR(UNKNOWN_LONG_OPT, "unknown option: --%s", parsed_args.err.str)
ARGS_ERR(
    UNKNOWN_SHORT_OPT,
    "unknown option: -%s",
    escape_char(parsed_args.err.unknown_short_opt)
)

#ifdef DEFINE_ARGS_ERR
#undef DEFINE_ARGS_ERR
#endif /* DEFINE_ARGS_ERR */
#ifdef PRINT_ARGS_ERR
#pragma GCC diagnostic pop
#undef PRINT_ARGS_ERR
#endif /* PRINT_ARGS_ERR */
#undef ARGS_ERR
