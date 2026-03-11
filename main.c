/* SPDX-FileCopyrightText: 2024 - 2026 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A Brainfuck to Linux ELF compiler. */

#ifndef BFC_TEST
/* C99 */
#include <assert.h>
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

/* return the output name for a source file with the provided filename
 *
 * If the filename does not end with '.' followed by the source extension
 * returns NULL.
 *
 * If an output extension is set, it appends it to the output name. */

static char *gen_outname(const char *filename, const RunConfig *rc) {
    size_t alloc_sz = strlen(filename) + 1;
    if (rc->output_extension) {
        size_t outname_alloc_sz = alloc_sz - strlen(rc->source_extension) +
                                  strlen(rc->output_extension);
        if (outname_alloc_sz > alloc_sz) alloc_sz = outname_alloc_sz;
    }
    //* start with the original filename */
    char *outname = strcpy(checked_malloc(alloc_sz), filename);

    assert(rc->source_extension[0]);

    char *dot = strrchr(outname, '.');
    if ((!dot) || (strcmp(dot + 1, rc->source_extension) != 0)) {
        free(outname);
        return NULL;
    }

    if (rc->output_extension) {
        strcpy(&dot[1], rc->output_extension);
    } else {
        *dot = '\0';
    }
    return outname;
}

/* compile a file */
static bool compile_file(const char *filename, const RunConfig *rc) {
    char *outname = gen_outname(filename, rc);
    if (!outname) {
        display_err((BFCError){
            .file = filename,
            .msg.ref = "File does not end with proper extension",
            .id = BF_ERR_BAD_EXTENSION,
        });
        return false;
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
                    list_targets();
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
    return EXIT_FAILURE;
}

#else /* BFC_TEST */
int run_tests(void);

int main(void) {
    return run_tests();
}
#endif /* BFC_TEST */
