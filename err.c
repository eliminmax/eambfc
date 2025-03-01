/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Handle error messages */

/* C99 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* internal */
#include "attributes.h"
#include "err.h"
#include "types.h"

/* to keep the order consistent with the bf_err_id enum, do non-ICE errors then
 * ICEs, with each group sorted alphabetically */
const char ERR_IDS[][32] = {
    "BAD_EXTENSION",
    "BUF_TOO_LARGE",
    "FAILED_READ",
    "FAILED_WRITE",
    "IMMEDIATE_TOO_LARGE",
    "JUMP_TOO_LONG",
    "MGR_ATEXIT_FAILED",
    "MISSING_OPERAND",
    "MULTIPLE_ARCHES",
    "MULTIPLE_EXTENSIONS",
    "MULTIPLE_OUTPUT_EXTENSIONS",
    "MULTIPLE_TAPE_BLOCK_COUNTS",
    "NOT_NUMERIC",
    "NO_SOURCE_FILES",
    "NO_TAPE",
    "OPEN_R_FAILED",
    "OPEN_W_FAILED",
    "TAPE_TOO_LARGE",
    "TOO_MANY_INSTRUCTIONS",
    "TOO_MANY_NESTED_LOOPS",
    "UNKNOWN_ARCH",
    "UNKNOWN_ARG",
    "UNMATCHED_CLOSE",
    "UNMATCHED_OPEN",
    "WRITE_TOO_LARGE",
    /* ICE divider */
    "ICE:APPEND_OBJ_TO_NULL",
    "ICE:INVALID_IR",
    "ICE:INVALID_JUMP_ADDRESS",
    "ICE:MGR_CLOSE_UNKNOWN",
    "ICE:MGR_FREE_UNKNOWN",
    "ICE:MGR_REALLOC_UNKNOWN",
    "ICE:PARAMS_TOO_LONG",
    "ICE:TOO_MANY_ALLOCS",
    "ICE:TOO_MANY_OPENS",
};

static out_mode err_mode = OUTMODE_NORMAL;

/* the only external access to the variables is through these functions. */
void quiet_mode(void) {
    if (err_mode == OUTMODE_NORMAL) err_mode = OUTMODE_QUIET;
}

void json_mode(void) {
    err_mode = OUTMODE_JSON;
}

/* avoid using json_str for this special case, as malloc may fail again,
 * causing a loop of failures to generate json error messages properly.
 * If puts or fputs call malloc internally, then there's nothing to be done. */
noreturn void alloc_err(void) {
    switch (err_mode) {
    case OUTMODE_JSON:
        puts(
            "{\"errorId:\":\"ALLOC_FAILED\","
            "\"message\":\"A call to malloc or realloc returned NULL.\"}"
        );
        break;
    case OUTMODE_NORMAL:
        fputs(
            "Error ALLOC_FAILED: A call to malloc or realloc returned NULL.\n",
            stderr
        );
        break;
    case OUTMODE_QUIET: break;
    }
    exit(EXIT_FAILURE);
}

nonnull_args static int char_esc(char c, char *dest) {
    /* escaped characters with single-letter ids */
    switch ((uchar)c) {
    case '\n': return sprintf(dest, "\\n");
    case '\r': return sprintf(dest, "\\r");
    case '\f': return sprintf(dest, "\\f");
    case '\t': return sprintf(dest, "\\t");
    case '\b': return sprintf(dest, "\\b");
    default: break;
    }
    /* ASCII control characters */
    if ((uchar)c < 040) { return sprintf(dest, "\\x%02hhx", (uchar)c); }
    /* non-ASCII */
    if ((uchar)c & 0x80) { return sprintf(dest, "�"); }
    /* default */
    return sprintf(dest, "%c", c);
}

/* return a pointer to a JSON-escaped version of the input string
 * calling function is responsible for freeing it */
nonnull_ret nonnull_args static char *json_str(const char *str) {
    size_t bufsz = 4096;
    const char *p = str;
    char *json_escaped = malloc(bufsz);
    if (json_escaped == NULL) alloc_err();
    char *outp = json_escaped;
    while (*p) {
        switch (*p) {
        case '\n': outp += sprintf(outp, "\\n"); break;
        case '\r': outp += sprintf(outp, "\\r"); break;
        case '\f': outp += sprintf(outp, "\\f"); break;
        case '\t': outp += sprintf(outp, "\\t"); break;
        case '\b': outp += sprintf(outp, "\\b"); break;
        case '\\': outp += sprintf(outp, "\\\\"); break;
        case '\"': outp += sprintf(outp, "\\\""); break;
        default:
            /* would prefer a pure switch statement, but `case a ... d` is
             * a non-standard (though common) extension to C, and is not
             * portable.
             * Using this `if`-within-a-`switch` instead. */
            if ((uchar)(*p) < 040) { /* control chars are 000 to 037 */
                outp += sprintf(outp, "\\u%04hhx", (uchar)*p);
            }
            /* the true default case. Character needs no escaping. */
            *(outp++) = *p;
        }
        /* If less than 8 chars are left before overflow, allocate more space */
        if ((size_t)outp - (size_t)json_escaped > bufsz - 8) {
            bufsz += 4096;
            char *reallocator = realloc(json_escaped, bufsz);
            if (reallocator == NULL) {
                free(json_escaped);
                alloc_err();
            }
            json_escaped = reallocator;
        }
        p++;
    }
    *outp = 0;
    return json_escaped;
}

static void normal_eprint(bf_comp_err err) {
    if (err.msg == NULL) abort();
    /* 75 is the maximum possible length needed, assuming that size_t is at
     * most 128 bits long. If it's longer, a file would need to be at least tens
     * of yottabytes in size to need more than 75, which is unsupported. */
    char extra_info[75] = {'\0'};
    int i = 0;
    if (err.has_location) {
        i = sprintf(
            extra_info,
            /* assuming 128-bit uintmax_t, and a file with over 2^126 lines and
             * over 2^126 columns, this could take up at most 56 bytes. That
             * file would take at least 12.9 yottabytes, so it's already
             * unreasonably large, but 56 is still small enough to accommodate
             * for. */
            " at line %ju, column %ju",
            (uintmax_t)err.line,
            (uintmax_t)err.col
        );
    }
    if (err.has_instr) {
        /* this will be 15-18 non-null characters long */
        strcpy(&extra_info[i], " (instruction ");
        i += 14;
        i += char_esc(err.instr, &extra_info[i]);
        extra_info[i++] = ')';
        extra_info[i] = '\0';
    }
    if (err.file != NULL) {
        fprintf(
            stderr,
            "Error %s in file %s %s: %s\n",
            ERR_IDS[err.id],
            err.file,
            extra_info,
            err.msg
        );
    } else {
        fprintf(
            stderr, "Error %s %s: %s\n", ERR_IDS[err.id], extra_info, err.msg
        );
    }
}

static void json_eprint(bf_comp_err err) {
    if (err.msg == NULL) abort();
    char *msg = json_str(err.msg);
    if (msg == NULL) alloc_err();

    printf("{\"errorId\": \"%s\", ", ERR_IDS[err.id]);
    if (err.file != NULL) {
        char *filename = json_str(err.file);
        printf("\"file\": \"%s\", ", filename);
        free(filename);
    }
    if (err.has_location) {
        printf(
            "\"line\": %ju, \"column\": %ju, ",
            (uintmax_t)err.line,
            (uintmax_t)err.col
        );
    }
    if (err.has_instr) {
        if ((schar)err.instr > 040) {
            switch (err.instr) {
            case '\\': fputs("\"instruction\": \"\\\\\", ", stdout); break;
            case '"': fputs("\"instruction\": \"\\\\\", ", stdout); break;
            default: printf("\"instruction\": \"%c\", ", err.instr);
            }
        } else if ((uchar)err.instr & 0x80) {
            printf("\"instruction\": \"�\"");
        } else {
            switch (err.instr) {
            case '\n': fputs("\"instruction\": \"\\n\", ", stdout); break;
            case '\r': fputs("\"instruction\": \"\\r\", ", stdout); break;
            case '\f': fputs("\"instruction\": \"\\f\", ", stdout); break;
            case '\t': fputs("\"instruction\": \"\\t\", ", stdout); break;
            case '\b': fputs("\"instruction\": \"\\b\", ", stdout); break;
            default:
                printf("\"instruction\": \"\\u%04hhx\", ", (uchar)err.instr);
            }
        }
    }
    printf("\"message\": \"%s\"}\n", msg);
    free(msg);
}

void display_err(bf_comp_err e) {
    switch (err_mode) {
    case OUTMODE_QUIET: break;
    case OUTMODE_NORMAL: normal_eprint(e); break;
    case OUTMODE_JSON: json_eprint(e); break;
    }
}

noreturn nonnull_args void internal_err(bf_err_id err_id, const char *msg) {
    bf_comp_err e = {
        .msg = msg,
        .file = NULL,
        .has_instr = false,
        .has_location = false,
        .id = err_id
    };
    display_err(e);
    exit(EXIT_FAILURE);
}
