/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A Brainfuck to x86_64 Linux ELF compiler. */

/* C99 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
/* internal */
#include "config.h"
#include "compat/eambfc_inttypes.h"
#include "eam_compile.h"
#include "err.h"

/* Return the permission mask to use for the output file */
mode_t getPerms(void) {
    /* The umask function sets the file mode creation mask to its argument and
     * returns the previous mask.
     *
     * The mask is used to set default file mode
     * the default permission for directories is (0777 & ~mask).
     * the default permission for normal files is (0666 & ~mask).
     *
     * There is no standard way to query the mask without changing it.
     * Because of this, the proper way to query the mask is to do the following.
     * Seriously. */
    mode_t mask = umask(0022); umask(mask);
    /* default to the default file permissions for group and other, but rwx for
     * the owner. */
    mode_t perms = S_IRWXU | (~mask & 0066);
    /* if the file's group can read it, it should also be allowed to execute it.
     * the same goes for other users. */
    if (perms & S_IRGRP) perms |= S_IXGRP;
    if (perms & S_IROTH) perms |= S_IXOTH;
    return perms;
}


/* print the help message to outfile. progname should be argv[0]. */
void showHelp(FILE *outfile, char *progname) {
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
        " -e ext    - (only provide once) use 'ext' as the extension for\n"
        "             source files instead of '.bf'\n"
        "             (This program will remove this at the end of the input\n"
        "             file to create the output file name)\n"
        "\n"
        "* -q and -j will not affect arguments passed before they were.\n"
        "\n"
        "** Optimization will mess with error reporting, as error locations\n"
        "   will be location in the intermediate representation text, rather\n"
        "   than the source code.\n"
        "\n"
        "Remaining options are treated as source file names. If they don't\n"
        "end with '.bf' (or the extension specified with '-e'), the program\n"
        "will abort.\n\n",
        progname
    );
}

/* remove ext from end of str. If ext is not in str, return false. */
bool rmExt(char *str, const char *ext) {
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
#define SHOW_HINT() if (!(quiet || json)) showHelp(stderr, argv[0])

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
    mode_t perms = getPerms();
    char char_str_buf[2] = { '\0', '\0' };

    while ((opt = getopt(argc, argv, ":hVqjOkme:")) != -1) {
        switch(opt) {
          case 'h':
            showHelp(stdout, argv[0]);
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
                " * tape size: %d 4-KiB blocks\n"
                " * max nesting level: %d\n"
                " * max compiler errors shown: %d\n"
                " * %s\n", /* git info or message stating git not used. */
                argv[0],
                EAMBFC_VERSION,
                TAPE_BLOCKS,
                MAX_NESTING_LEVEL,
                MAX_ERROR,
                EAMBFC_COMMIT
            );
            return EXIT_SUCCESS;
          case 'q':
            quiet = true;
            quietMode();
            break;
          case 'j':
            json = true;
            jsonMode();
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
                basicError("MULTIPLE_EXTENSIONS", "passed -e multiple times.");
                SHOW_HINT();
                return EXIT_FAILURE;
            }
            ext = optarg;
            break;
          case ':': /* -e without an argument */
            char_str_buf[0] = (char) optopt;
            parameterError(
                "MISSING_OPERAND",
                "{} requires an additional argument",
                char_str_buf
            );
            return EXIT_FAILURE;
          case '?': /* unknown argument */
            char_str_buf[0] = (char) optopt;
            parameterError(
                "UNKNOWN_ARG",
                "Unknown argument: {}.",
                char_str_buf
            );
            return EXIT_FAILURE;
        }
    }
    if (optind == argc) {
        basicError("NO_SOURCE_FILES", "No source files provided.");
        SHOW_HINT();
        return EXIT_FAILURE;
    }

    /* if no extension was provided, use .bf */
    if (strlen(ext) == 0) ext = ".bf";

    for (/* reusing optind here */; optind < argc; optind++) {
        outname = malloc(strlen(argv[optind]) + 1);
        if (outname == NULL) {
            allocError();
            ret = EXIT_FAILURE;
            if (moveahead) continue; else break;
        }
        strcpy(outname, argv[optind]);
        if (!rmExt(outname, ext)) {
            parameterError(
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
            parameterError(
                "OPEN_R_FAILED",
                "Failed to open {} for reading.",
                argv[optind]
            );
            free(outname);
            ret = EXIT_FAILURE;
            if (moveahead) continue; else break;
        }
        dst_fd = open(outname, O_WRONLY+O_CREAT+O_TRUNC, perms);
        if (dst_fd < 0) {
            parameterError(
                "OPEN_W_FAILED",
                "Failed to open {} for writing.",
                outname
            );
            close(src_fd);
            ret = EXIT_FAILURE;
            free(outname);
            if (moveahead) continue; else break;
        }
        result = bfCompile(src_fd, dst_fd, optimize);
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
