/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A basic non-optimizing Brainfuck to x86_64 Linux ELF compiler.
 * */

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
#include "eam_compile.h"

/* Return the permission mask to use for the output file */
mode_t _getperms(void) {
    /* The umask function sets the file mode creation mask to its argument and
     * returns the previous mask.
     *
     * The mask is used to set default file mode
     * the default permission for directories is (0777 & ~mask).
     * the default permission for normal files is (0666 & ~mask).
     *
     * There is no standard way to query the mask.
     * Because of this, the proper way to query the mask is to do the following.
     * Seriously. */
    mode_t mask = umask(0022); umask(mask);
    /* default to the default file permissions for group and other, but rwx for
     * the owner. */
    mode_t permissions = S_IRWXU | (~mask & 066);
    /* if the file's group can read it, it should also be allowed to execute it.
     * the same goes for other users. */
    if (permissions & S_IRGRP) {
        permissions += S_IXGRP;
    }
    if (permissions & S_IROTH) {
        permissions += S_IXOTH;
    }
    return permissions;
}


/* print the help message to outfile. progname should be argv[0]. */
void _showhelp(FILE *outfile, char *progname) {
    fprintf(outfile,
            "Usage: %s [options] <program.bf> [<program2.bf> ...]\n\n"
            " -h        - display this help text\n"
            " -q        - don't print compilation errors.\n"
            " -k        - keep files that failed to compile for debugging\n"
            " -m        - 'Move ahead' to the next file instead of quitting if\n"
            "             a file fails to compile\n"
            " -e ext    - use 'ext' as the extension for source files instead\n"
            "             of '.bf' (This program will remove this at the end \n"
            "             of the input file to create the output file name)\n\n"
            "Remaining options are treated as source file names. If they don't"
            "\nend with '.bf' (or the extension specified with '-e'), the \n"
            "program will abort.\n\n",
            progname);
}

/* check if str ends with ext. If so, remove ext from the end and return a
 * truthy value. If not, return 0. */
int _rmext(char *str, const char *ext) {
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


/* macro for use in main function only.
 *
 * unless -q was passed, print an error message to stderr using a printf-like
 * interface.
 *
 * exit out of main right afterwards, whether or not -q was passed.
 * */
#define errorout(...) if (!quiet) {fprintf(stderr, __VA_ARGS__);}\
    return EXIT_FAILURE

int main(int argc, char* argv[]) {
    int srcFD, dstFD;
    int result;
    int opt;
    /* default to empty string. */
    char *ext = "";
    /* default to false, set to true if relevant argument was passed. */
    bool quiet = false, keep = false, moveahead = false;
    mode_t permissions = _getperms();

    while ((opt = getopt(argc, argv, "hqkme:")) != -1) {
        switch(opt) {
            case 'h':
                _showhelp(stdout, argv[0]);
                return EXIT_SUCCESS;
            case 'q':
                quiet = true;
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
                    errorout("provided -e multiple times!\n");
                }
                ext = optarg;
                break;
            case ':': /* -e without an argument */
                errorout("%c requires an additional argument.\n", optopt);
            case '?': /* unknown argument */
                errorout("Unknown argument: %c.\n", optopt);
        }
    }

    if (optind == argc) {
        fputs("No source files provided.\n", stderr);
        return EXIT_FAILURE;
    }
    if (strlen(ext) == 0) {
        ext = ".bf";
    }

    for (/* optind already exists */; optind < argc; optind++) {

        srcFD = open(argv[optind], O_RDONLY);
        if (srcFD < 0) {
            errorout("Failed to open source file for reading.\n");
        }
        if (! _rmext(argv[optind], ext)) {
            errorout("%s does not end with %s.\n", argv[optind], ext);
        }
        dstFD = open(argv[optind], O_WRONLY+O_CREAT+O_TRUNC, permissions);
        if (dstFD < 0) {
            errorout("Failed to open destination file for writing.\n");
        }
        result = bfCompile(srcFD, dstFD, keep);
        close(srcFD);
        close(dstFD);
        if ((!result) && (!quiet)) {
            fprintf(
                stderr,
                "%s%s: Failed to compile character %c at line %d, column %d.\n"
                "Error message: \"%s\"\n",
                argv[optind], ext, /* sneaky way to hide the editing of argv */
                currentInstruction,
                currentInstructionLine,
                currentInstructionColumn,
                errorMessage
            );
            if (!keep) {
                remove(argv[optind]);
            }
            if (!moveahead) {
                return EXIT_FAILURE;
            }
        }
    }
    return EXIT_SUCCESS;
}
#undef errorout
