/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A Brainfuck to x86_64 Linux ELF compiler. */

/* C99 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <fcntl.h>
#include <unistd.h>
/* internal */
#include "compile.h"
#include "err.h"
#include "parse_args.h"
#include "resource_mgr.h"
#include "types.h"

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
static bool compile_file(const char *filename, const run_cfg *rc) {
    char *outname = mgr_malloc(strlen(filename) + 1);
    strcpy(outname, filename);

    if (!rm_ext(outname, rc->ext)) {
        param_err(
            "BAD_EXTENSION",
            "File {} does not end with expected extension.",
            filename
        );
        mgr_free(outname);
        return false;
    }
    if (rc->out_ext != NULL) {
        size_t outname_sz = strlen(outname);
        outname = mgr_realloc(outname, outname_sz + strlen(rc->out_ext) + 1);
        strcat(outname, rc->out_ext);
    }

    int src_fd = mgr_open(filename, O_RDONLY);
    if (src_fd < 0) {
        param_err("OPEN_R_FAILED", "Failed to open {} for reading.", filename);
        mgr_free(outname);
        return false;
    }
    int dst_fd = mgr_open_m(outname, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (dst_fd < 0) {
        param_err("OPEN_W_FAILED", "Failed to open {} for writing.", outname);
        mgr_close(src_fd);
        mgr_free(outname);
        return false;
    }
    bool result =
        bf_compile(rc->inter, src_fd, dst_fd, rc->optimize, rc->tape_blocks);
    if ((!result) && (!rc->keep)) remove(outname);
    mgr_close(src_fd);
    mgr_close(dst_fd);
    mgr_free(outname);
    return result;
}

int main(int argc, char *argv[]) {
    /* register atexit function to clean up any open files or memory allocations
     * left behind. */
#ifndef SKIP_RESOURCE_MGR
    register_mgr();
#endif /* SKIP_RESOURCE_MGR */
    int ret = EXIT_SUCCESS;
    run_cfg rc = parse_args(argc, argv);
    for (int i = optind; i < argc; i++) {
        if (compile_file(argv[i], &rc)) continue;
        ret = EXIT_FAILURE;
        if (!rc.moveahead) break;
    }

    return ret;
}
