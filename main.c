/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A Brainfuck to x86_64 Linux ELF compiler. */

/* C99 */
#include <stdio.h> /* FILE, stderr, stdout, printf, fprintf */
#include <stdlib.h> /* malloc, free, EXIT_*, strtoull */
#include <string.h> /* strncmp, strlen, strcpy */
/* POSIX */
#include <fcntl.h> /* open, O_*, mode_t */
#include <unistd.h> /* getopt, optopt, close */
/* internal */
#include "arch_inter.h" /* arch_inter, X86_64_INTER */
#include "compile.h" /* bf_compile */
#include "config.h" /* EAMBFC_* */
#include "err.h" /* *_err */
#include "types.h" /* bool, uint64_t, UINT64_MAX */

/* print the help message to outfile. progname should be argv[0]. */
void show_help(FILE *outfile, char *progname) {
    fprintf(outfile,
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

/* remove ext from end of str. If ext is not in str, return false. */
bool rm_ext(char *str, const char *ext) {
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


/* macro for use in main function only.
 * SHOW_HINT:
 *  * unless -q or -j was passed, write the help text to stderr. */
#define SHOW_HINT() if (!(quiet || json)) show_help(stderr, argv[0])

int main(int argc, char* argv[]) {
    int src_fd, dst_fd;
    int result;
    int opt;
    int ret = EXIT_SUCCESS;
    char *outname;
    /* default to empty string. */
    char *ext = "";
    /* default to false, set to true if relevant argument was passed. */
    bool quiet = false, keep = false, moveahead = false, json = false;
    bool optimize = false;
    char char_str_buf[2] = { '\0', '\0' };
    uint64_t tape_blocks = 0;

    while ((opt = getopt(argc, argv, ":hVqjOkmAe:t:")) != -1) {
        switch(opt) {
          case 'h':
            show_help(stdout, argv[0]);
            return EXIT_SUCCESS;
          case 'V':
            printf(
                "%s: eambfc version %s\n\n"
                "Copyright (c) 2024 Eli Array Minkoff.\n"
                "License: GNU GPL version 3 "
                "<https://gnu.org/licenses/gpl.html>.\n"
                "This is free software: "
                "you are free to change and redistribute it.\n"
                "There is NO WARRANTY, to the extent permitted by law.\n\n"
                "Build configuration:\n"
                " * %s\n", /* git info or message stating git not used. */
                argv[0],
                EAMBFC_VERSION,
                EAMBFC_COMMIT
            );
            return EXIT_SUCCESS;
          case 'A':
            printf(
                "This build of %s supports the following architectures:\n\n"
                "- x86_64 (aliases: x64, amd64, x86-64)\n"
#ifdef EAM_TARGET_ARM64
                "- arm64 (aliases: aarch64)\n"
#endif /* EAM_TARGET_ARM64 */
                "\nIf no architecture is specified, it defaults to x86_64.\n",
                argv[0]
            );
            return EXIT_SUCCESS;

          case 'q':
            quiet = true;
            quiet_mode();
            break;
          case 'j':
            json = true;
            json_mode();
            break;
          case 'O':
            optimize = true;
            break;
          case 'k':
            keep = true;
            break;
          case 'm':
            moveahead = true;
            break;
          case 'e':
            /* Print an error if ext was already set. */
            if (strlen(ext) > 0) {
                basic_err("MULTIPLE_EXTENSIONS", "passed -e multiple times.");
                SHOW_HINT();
                return EXIT_FAILURE;
            }
            ext = optarg;
            break;
          case 't':
            /* Print an error if tape_blocks has already been set */
            if (tape_blocks != 0) {
                basic_err(
                    "MULTIPLE_TAPE_BLOCK_COUNTS",
                    "passed -t multiple times."
                );
                SHOW_HINT();
                return EXIT_FAILURE;
            }
            char *endptr;
            // casting unsigned long long instead of using scanf as scanf can
            // lead to undefined behavior if input isn't well-crafted, and
            // unsigned long long is guaranteed to be at least 64 bits.
            unsigned long long holder = strtoull(optarg, &endptr, 10);
            // if the full opt_arg wasn't consumed, it's not a numeric value.
            if (*endptr != '\0') {
                param_err(
                    "NOT_NUMERIC",
                    "{} could not be parsed as a numeric value",
                    optarg
                );
                SHOW_HINT();
                return EXIT_FAILURE;
            }
            if (holder == 0) {
                basic_err(
                    "NO_TAPE",
                    "Tape value for -t must be at least 1"
                );
                SHOW_HINT();
                return EXIT_FAILURE;
            }
            /* if it's any larger than this, the tape size would exceed the
             * 64-bit integer limit. */
            if (holder >= UINT64_MAX >> 12) {
                param_err(
                    "TAPE_TOO_LARGE",
                    "{} * 0x1000 exceeds the 64-bit integer limit.",
                    optarg
                );
                SHOW_HINT();
                return EXIT_FAILURE;
            }
            tape_blocks = (uint64_t) holder;
            break;
          case ':': /* -e or -t without an argument */
            char_str_buf[0] = (char) optopt;
            param_err(
                "MISSING_OPERAND",
                "{} requires an additional argument",
                char_str_buf
            );
            return EXIT_FAILURE;
          case '?': /* unknown argument */
            char_str_buf[0] = (char) optopt;
            param_err(
                "UNKNOWN_ARG",
                "Unknown argument: {}.",
                char_str_buf
            );
            return EXIT_FAILURE;
        }
    }
    if (optind == argc) {
        basic_err("NO_SOURCE_FILES", "No source files provided.");
        SHOW_HINT();
        return EXIT_FAILURE;
    }

    /* if no extension was provided, use .bf */
    if (strlen(ext) == 0) ext = ".bf";

    /* if no tape size was specified, default to 8. */
    if (tape_blocks == 0) tape_blocks = 8;

    for (/* reusing optind here */; optind < argc; optind++) {
        outname = malloc(strlen(argv[optind]) + 1);
        if (outname == NULL) {
            alloc_err();
            ret = EXIT_FAILURE;
            if (moveahead) continue; else break;
        }
        strcpy(outname, argv[optind]);
        if (!rm_ext(outname, ext)) {
            param_err(
                "BAD_EXTENSION",
                "File {} does not end with expected extension.",
                argv[optind]
            );
            ret = EXIT_FAILURE;
            free(outname);
            if (moveahead) continue; else break;
        }
        src_fd = open(argv[optind], O_RDONLY);
        if (src_fd < 0) {
            param_err(
                "OPEN_R_FAILED",
                "Failed to open {} for reading.",
                argv[optind]
            );
            free(outname);
            ret = EXIT_FAILURE;
            if (moveahead) continue; else break;
        }
        dst_fd = open(outname, O_WRONLY|O_CREAT|O_TRUNC, 0755);
        if (dst_fd < 0) {
            param_err(
                "OPEN_W_FAILED",
                "Failed to open {} for writing.",
                outname
            );
            close(src_fd);
            ret = EXIT_FAILURE;
            free(outname);
            if (moveahead) continue; else break;
        }
        result = bf_compile(X86_64_INTER, src_fd, dst_fd, optimize, tape_blocks);
        close(src_fd);
        close(dst_fd);
        if (!result) {
            if (!keep) remove(outname);
            ret = EXIT_FAILURE;
            free(outname);
            if (moveahead) continue; else break;
        }
        free(outname);
    }

    return ret;
}
