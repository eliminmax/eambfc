/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A basic non-optimizing Brainfuck to x86_64 Linux ELF compiler. */

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
#include "config.h"
#include "eam_compile.h"
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
        " -h        - display this help text and exit.\n"
        " -V        - print version information and exit.\n"
        " -q        - don't print compilation errors.\n"
        " -j        - print compilation errors in JSON-like format.\n"
        "             (assumes file names are UTF-8-encoded.)\n"
        " -k        - keep files that failed to compile (for debugging)\n"
        " -c        - continue to the next file instead of quitting if a\n"
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
 * showError:
 *  * unless -q was passed, print an error message to stderr using fprintf
 *
 * fileFail:
 *  * call if compiling a file failed. Depending on moveahead, either
 *    exit immediately or set the return code to EXIT_FAILURE for later.
 *
 * showHint:
 *  * unless -q was passed, write the help text to stderr. */
#define showError(...) if (!quiet) fprintf(stderr, __VA_ARGS__)
#define fileFail() if (moveahead) ret = EXIT_FAILURE; else return EXIT_FAILURE
#define showHint() if (!quiet) showHelp(stderr, argv[0])

int main(int argc, char* argv[]) {
    int srcFD, dstFD;
    int result;
    int opt;
    int ret = EXIT_SUCCESS;
    char *outname;
    char *err_msg_json;
    char *out_name_json;
    char *in_name_json;
    /* default to empty string. */
    char *ext = "";
    /* default to false, set to true if relevant argument was passed. */
    bool quiet = false, keep = false, moveahead = false, json = false;
    mode_t perms = getPerms();

    while ((opt = getopt(argc, argv, ":hVqjkme:")) != -1) {
        switch(opt) {
          case 'h':
            showHelp(stdout, argv[0]);
            return EXIT_SUCCESS;
          case 'V':
            printf("%s: eambfc version %s.\n", argv[0], EAMBFC_VERSION);
            return EXIT_SUCCESS;
          case 'q':
            quiet = true;
            break;
          case 'j':
            json = true;
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
                if (json) {
                    printf("{\"errorId\":\"MULTIPLE_EXTENSIONS\"}\n");
                } else {
                    showError("provided -e multiple times!\n");
                    showHint();
                }
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
                showError("%c requires an additional argument.\n", optopt);
                showHint();
            }
            return EXIT_FAILURE;
          case '?': /* unknown argument */
            if (json) {
                printf(
                    "{\"errorId\":\"UNKNOWN_ARG\",\"argument\":\"%c\"}\n",
                    optopt
                );
            } else {
                showError("Unknown argument: %c.\n", optopt);
                showHint();
            }
            return EXIT_FAILURE;
        }
    }
    if (optind == argc) {
        if (json) {
            printf("{\"errorId\":\"NO_SOURCE_FILES\"}\n");
        } else {
            showError("No source files provided.\n");
            showHint();
        }
        return EXIT_FAILURE;
    }

    /* if no extension was provided, use .bf */
    if (strlen(ext) == 0) {
        ext = ".bf";
    }

    for (/* reusing optind here */; optind < argc; optind++) {
        outname = (char *)malloc(strlen(argv[optind]) + 1);
        if (outname == NULL) {
            showError(
                "malloc failure when determining output file name! Aborting.\n"
            );
            exit(EXIT_FAILURE);
        }
        if (json) {
            in_name_json = jsonStr(argv[optind]);
            out_name_json = jsonStr(outname);
        }
        strcpy(outname, argv[optind]);
        srcFD = open(argv[optind], O_RDONLY);
        if (srcFD < 0) {
            if (json) {
                printf(
                    "{\"errorId\":\"OPEN_R_FAILED\",\"file\":\"%s\"}\n",
                    in_name_json
                );
            } else {
                showError("Failed to open %s for reading.\n", argv[optind]);
            }
            fileFail();
        }
        if (! rmExt(outname, ext)) {
            if (json) {
                printf(
                    "{\"errorId\":\"BAD_EXTENSION\",\"file\":\"%s\"}\n",
                    argv[optind]
                );
            } else {
                showError("%s does not end with %s.\n", argv[optind], ext);
            }
            fileFail();
        }
        dstFD = open(outname, O_WRONLY+O_CREAT+O_TRUNC, perms);
        if (dstFD < 0) {
            if (json) {
                printf(
                    "{\"errorId\":\"OPEN_W_FAILED\",\"file\":\"%s\"}\n",
                    outname
                );
            } else {
                showError(
                    "Failed to open destination file %s for writing.\n",
                    outname
                );
            }
            if (moveahead) {
                close(srcFD);
            } else {
                return EXIT_FAILURE;
            }
        }
        result = bfCompile(srcFD, dstFD);
        close(srcFD);
        close(dstFD);
        if (!result) {
            for(uint8_t i = 0; i < MAX_ERROR && err_list[i].active; i++) {
                if (json) {
                    err_msg_json = jsonStr(err_list[i].err_msg);
                    printf(
                        "{\"errorId\":\"%s\",\"file\":\"%s\",\"line\":%d,"
                        "\"column\":%d,\"message\":\"%s\"}\n",
                        err_list[i].err_id,
                        argv[optind],
                        err_list[i].line,
                        err_list[i].col,
                        err_msg_json
                    );
                    free(err_msg_json);
                } else {
                    showError(
                        "%s: Failed to compile '%c' at line %d, column %d.\n"
                        "Error ID: %s\n"
                        "Error message: \"%s\"\n",
                        argv[optind],
                        err_list[i].instr,
                        err_list[i].line,
                        err_list[i].col,
                        err_list[i].err_id,
                        err_list[i].err_msg
                    );
                }
            }
            if (!keep) remove(outname);
            fileFail();
        }
        if (json) {
            free(in_name_json);
            free(out_name_json);
        }
        free(outname);
    }

    return ret;
}
