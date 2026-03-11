/* SPDX-FileCopyrightText: 2024 - 2026 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A Brainfuck to 64-bit Linux ELF compiler. */

#ifndef BFC_TEST
/* C99 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <fcntl.h>
#include <unistd.h>
/* internal */
#include <types.h>

#include "compile.h"
#include "err.h"
#include "setup.h"
#include "util.h"

/* remove ext from end of str. If str doesn't end with ext, return false. */
static bool rm_ext(char *str, const char *ext) {
    size_t strsz = strlen(str);
    size_t extsz = strlen(ext);
    /* strsz must be at least 1 character longer than extsz to continue. */
    if (strsz <= extsz) return false;
    /* because of the above check, distance is known to be a positive value. */
    size_t distance = strsz - extsz;
    /* return 0 if str does not end in ext */
    if (strncmp(str + distance, ext, extsz) != 0) return false;
    /* set the beginning of the match to the null byte, to end str early */
    str[distance] = false;
    return true;
}

/* compile a file */
static bool compile_file(const char *filename, const RunConfig *rc) {
    char *outname = checked_malloc(strlen(filename) + 1);
    strcpy(outname, filename);

    if (!rm_ext(outname, rc->source_extension)) {
        display_err((BFCError){
            .file = filename,
            .msg.ref = "File does not end with proper extension",
            .id = BF_ERR_BAD_EXTENSION,
        });
        free(outname);
        return false;
    }
    if (rc->output_extension != NULL) {
        size_t outname_sz = strlen(outname);
        outname = checked_realloc(
            outname, outname_sz + strlen(rc->output_extension) + 1
        );
        strcat(outname, rc->output_extension);
    }

    int src_fd = open(filename, O_RDONLY);
    if (src_fd < 0) {
        display_err((BFCError){
            .file = filename,
            .id = BF_ERR_OPEN_R_FAILED,
            .msg.ref = "Failed to open file for reading",
        });
        free(outname);
        return false;
    }
    int dst_fd = open(outname, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (dst_fd < 0) {
        display_err((BFCError){
            .file = outname,
            .id = BF_ERR_OPEN_W_FAILED,
            .msg.ref = "Failed to open file for writing",
        });
        close(src_fd);
        free(outname);
        return false;
    }
    bool result = bf_compile(
        rc->backend,
        filename,
        outname,
        src_fd,
        dst_fd,
        rc->optimize,
        rc->tape_blocks
    );
    if ((!result) && (!rc->keep)) remove(outname);
    close(src_fd);
    close(dst_fd);
    free(outname);
    return result;
}

static int standard_run(RunConfig *rc) {
    int ret = EXIT_SUCCESS;

    for (size_t i = 0; i < rc->nfiles; ++i) {
        if (!compile_file(rc->files[i], rc)) {
            ret = EXIT_FAILURE;
            if (!rc->continue_on_error) break;
        }
    }
    free(rc->files);
    return ret;
}

int main(int argc, char *argv[]) {
    ArgParseOut parsed_args;
    switch (parse_args(argc, argv, &parsed_args)) {
        case ARGS_OK:
            switch (parsed_args.ok.run_type) {
                case STANDARD_RUN:
                    return standard_run(&parsed_args.ok);
                case SHOW_VERSION:
                    show_version(argv[0] ? argv[0] : "eambfc");
                    return EXIT_SUCCESS;
                case SHOW_HELP:
                    show_help(argv[0] ? argv[0] : "eambfc", stdout);
                    return EXIT_SUCCESS;
                case LIST_TARGETS:
                    return EXIT_SUCCESS;
            }
            fprintf(
                stderr,
                "INTERNAL ERROR: invalid run type: %d\n",
                (int)parsed_args.ok.run_type
            );
            abort();
#define PRINT_ARGS_ERR
#include "arg_parse_errs.h"
    }
}

#else /* BFC_TEST */
int run_tests(void);

int main(void) {
    return run_tests();
}
#endif /* BFC_TEST */
