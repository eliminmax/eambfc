/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A Brainfuck to 64-bit Linux ELF compiler. */

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
#include "types.h"
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
static bool compile_file(const char *filename, const run_cfg *rc) {
    char *outname = checked_malloc(strlen(filename) + 1);
    strcpy(outname, filename);

    if (!rm_ext(outname, rc->ext)) {
        display_err((bf_comp_err){
            .file = filename,
            .msg.ref = "File does not end with proper extension",
            .id = BF_ERR_BAD_EXTENSION,
        });
        free(outname);
        return false;
    }
    if (rc->out_ext != NULL) {
        size_t outname_sz = strlen(outname);
        outname =
            checked_realloc(outname, outname_sz + strlen(rc->out_ext) + 1);
        strcat(outname, rc->out_ext);
    }

    int src_fd = open(filename, O_RDONLY);
    if (src_fd < 0) {
        display_err((bf_comp_err){
            .file = filename,
            .id = BF_ERR_OPEN_R_FAILED,
            .msg.ref = "Failed to open file for reading",
        });
        free(outname);
        return false;
    }
    int dst_fd = open(outname, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (dst_fd < 0) {
        display_err((bf_comp_err){
            .file = outname,
            .id = BF_ERR_OPEN_W_FAILED,
            .msg.ref = "Failed to open file for writing",
        });
        close(src_fd);
        free(outname);
        return false;
    }
    bool result = bf_compile(
        rc->inter,
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

#ifdef BFC_TEST
int real_main(int argc, char *argv[]) {
#else /* BFC_TEST */
int main(int argc, char *argv[]) {
#endif /* BFC_TEST */

    int ret = EXIT_SUCCESS;
    run_cfg rc = parse_args(argc, argv);
    for (int i = optind; i < argc; i++) {
        if (compile_file(argv[i], &rc)) continue;
        ret = EXIT_FAILURE;
        if (!rc.cont_on_fail) break;
    }
    return ret;
}
