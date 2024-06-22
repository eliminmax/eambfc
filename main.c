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
#include "json_escape.h"

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

/* check if str ends with ext. If so, remove ext from the end and return a
 * truthy value. If not, return 0. */
int rmExt(char *str, const char *ext) {
    size_t strsz = strlen(str);
    size_t extsz = strlen(ext);
    /* strsz must be at least 1 character longer than extsz to continue. */
    if (strsz <= extsz) {
        return 0;
    }
    /* because of the above check, distance is known to be a positive value. */
    size_t distance = strsz - extsz;
    /* return 0 if str does not end in extsz*/
    if (strncmp(str + distance, ext, extsz) != 0) {
        return 0;
    }
    /* set the beginning of the match to the null byte, to end str early */
    str[distance] = 0;
    return 1;
}


/* macros for use in main function only.
 * SHOW_ERROR:
 *  * unless -q was passed, print an error message to stderr using fprintf
 *
 * SHOW_HINT:
 *  * unless -q or -j was passed, write the help text to stderr. */
#define SHOW_ERROR(...) if (!quiet) fprintf(stderr, __VA_ARGS__)
#define SHOW_HINT() if (!(quiet || json)) showHelp(stderr, argv[0])

int main(int argc, char* argv[]) {
    int srcFD, dstFD;
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
            if (json) {
                printf(
                    "{\"errorId\":\"MISSING_OPERAND\","
                    "\"argument\":\"%c\"}\n",
                    optopt
                );
            } else {
                SHOW_ERROR("%c requires an additional argument.\n", optopt);
                SHOW_HINT();
            }
            return EXIT_FAILURE;
          case '?': /* unknown argument */
            if (json) {
                printf(
                    "{\"errorId\":\"UNKNOWN_ARG\",\"argument\":\"%c\"}\n",
                    optopt
                );
            } else {
                SHOW_ERROR("Unknown argument: %c.\n", optopt);
                SHOW_HINT();
            }
            return EXIT_FAILURE;
        }
    }
    if (optind == argc) {
        if (json) {
            printf("{\"errorId\":\"NO_SOURCE_FILES\"}\n");
        } else {
            SHOW_ERROR("No source files provided.\n");
            SHOW_HINT();
        }
        return EXIT_FAILURE;
    }

    /* if no extension was provided, use .bf */
    if (strlen(ext) == 0) {
        ext = ".bf";
    }

    for (/* reusing optind here */; optind < argc; optind++) {
        outname = malloc(strlen(argv[optind]) + 1);
        if (outname == NULL) {
            if (json) {
                printf(
                    "{\"errorId\":\"ICE_ICE_BABY\",\"message\":\"%s\"}",
                    "malloc failed when determining outfile name! Aborting."
                );

            } else {
                SHOW_ERROR(
                    "malloc failed when determining outfile name! Aborting.\n"
                );
            }
            exit(EXIT_FAILURE);
        }
        strcpy(outname, argv[optind]);
        srcFD = open(argv[optind], O_RDONLY);
        if (srcFD < 0) {
            if (json) {
                printJsonError(
                    "{\"errorId\":\"OPEN_R_FAILED\",\"file\":\"%s\"}\n",
                    argv[optind]
                );
            } else {
                SHOW_ERROR("Failed to open %s for reading.\n", argv[optind]);
            }
            free(outname);
            ret = EXIT_FAILURE;
            if (!moveahead) break;
        }
        if (! rmExt(outname, ext)) {
            if (json) {
                printJsonError(
                    "{\"errorId\":\"BAD_EXTENSION\",\"file\":\"%s\"}\n",
                    argv[optind]
                );
            } else {
                SHOW_ERROR("%s does not end with %s.\n", argv[optind], ext);
            }
            free(outname);
            ret = EXIT_FAILURE;
            if (!moveahead) break;
        }
        dstFD = open(outname, O_WRONLY+O_CREAT+O_TRUNC, perms);
        if (dstFD < 0) {
            if (json) {
                printJsonError(
                    "{\"errorId\":\"OPEN_W_FAILED\",\"file\":\"%s\"}\n",
                    outname
                );
            } else {
                SHOW_ERROR(
                    "Failed to open destination file %s for writing.\n",
                    outname
                );
            }
            free(outname);
            if (moveahead) {
                close(srcFD);
            } else {
                return EXIT_FAILURE;
            }
        }
        result = bfCompile(srcFD, dstFD, optimize);
        close(srcFD);
        close(dstFD);
        if (!result) {
            if (!keep) remove(outname);
            ret = EXIT_FAILURE;
            if (!moveahead) {
                free(outname);
                break;
            }
        }
        free(outname);
    }

    return ret;
}
