/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A basic non-optimizing Brainfuck to x86_64 Linux ELF compiler.
 * */

/* C99 */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
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
     * There is no standard way to query the mask without changing it.
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
        " -m        - Move ahead to the next file instead of quitting if a\n"
        "             file fails to compile\n"
        " -e ext    - (only provide once) use 'ext' as the extension for\n"
        "             source files instead of '.bf'.\n"
        "             (This program will remove this at the end of the input\n"
        "             file to create the output file name)\n\n"
        "Remaining options are treated as source file names. If they don't\n"
        "end with '.bf' (or the extension specified with '-e'), the program\n"
        "will abort.\n\n",
        progname
    );
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


/* macros for use in main function only.
 * showerror:
 *  * unless -q was passed, print an error message to stderr using fprintf
 *
 * filefail:
 *  * call if compiling a file failed. Depending on moveahead, either
 *    exit immediately or set the return code to EXIT_FAILURE for later.
 * 
 * showhint:
 *  * unless -q was passed, write the help text to stderr. */
#define showerror(...) if (!quiet) fprintf(stderr, __VA_ARGS__)
#define filefail() if (moveahead) ret = EXIT_FAILURE; else return EXIT_FAILURE
#define showhint() if (!quiet) _showhelp(stderr, argv[0])

int main(int argc, char* argv[]) {
    int srcFD, dstFD;
    int result;
    int opt;
    int ret = EXIT_SUCCESS;
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
                showerror("provided -e multiple times!\n");
                showhint();
                return EXIT_FAILURE;
            }
            ext = optarg;
            break;
          case ':': /* -e without an argument */
            showerror("%c requires an additional argument.\n", optopt);
            showhint();
            return EXIT_FAILURE;
          case '?': /* unknown argument */
            showerror("Unknown argument: %c.\n", optopt);
            showhint();
            return EXIT_FAILURE;
        }
    }
    if (optind == argc) {
        showerror("No source files provided.\n");
        showhint();
        return EXIT_FAILURE;
    }

    /* if no extension was provided, use .bf */
    if (strlen(ext) == 0) {
        ext = ".bf";
    }

    for (/* reusing optind here */; optind < argc; optind++) {
        srcFD = open(argv[optind], O_RDONLY);
        if (srcFD < 0) {
            showerror("Failed to open %s for reading.\n", argv[optind]);
            filefail();
        }
        if (! _rmext(argv[optind], ext)) {
            showerror("%s does not end with %s.\n", argv[optind], ext);
            filefail();
        }
        dstFD = open(argv[optind], O_WRONLY+O_CREAT+O_TRUNC, permissions);
        if (dstFD < 0) {
            showerror(
                "Failed to open destination file %s for writing.\n",
                argv[optind]
            );
            if (moveahead) {
                close(srcFD);
            } else {
                return EXIT_FAILURE;
            }
        }
        result = bfCompile(srcFD, dstFD, keep);
        close(srcFD);
        close(dstFD);
        if (!result) {
            for(uint8_t i = 0; i < MAX_ERROR && ErrorList[i].active; i++) {
                showerror(
                    "%s%s: Failed to compile character %c at line %d, column %d.\n"
                    "Error message: \"%s\"\n",
                    argv[optind], ext, /* sneaky way to hide the editing of argv */
                    ErrorList[i].currentInstruction,
                    ErrorList[i].currentInstructionLine,
                    ErrorList[i].currentInstructionColumn,
                    ErrorList[i].errorMessage
                );
            }
            if (!keep) remove(argv[optind]);
            filefail();
        }
    }

    return ret;
}
