/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Handle error messages */

/* C99 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* internal */
#include "attributes.h"
#include "config.h"
#include "err.h"
#include "resource_mgr.h"
#include "types.h"
#include "util.h"

#ifdef BFC_TEST
/* C99 */
#include <setjmp.h>
/* internal */
#include "unit_test.h"

/* bit shift e.id and or 1 into the lowest bit, to differentiate between
 * the original setjmp return value of 0, and the final one of the error id,
 * which also could be 0. */
#define ERR_CALLBACK(e_id) \
    if (testing_err) longjmp(etest_stack, e_id << 1 | 1)

#else /* BFC_TEST */
#define ERR_CALLBACK(e_id) (void)0
#endif /* BFC_TEST */

/* to keep the order consistent with the bf_err_id enum, do normal errors, then
 * ICEs, and finally "Fatal:AllocFailure", with each group sorted
 * alphabetically */
const char *ERR_IDS[] = {
    "BadSourceExtension",
    "BufferTooLarge",
    "FailedRead",
    "FailedWrite",
    "InputIsOutput",
    "JumpTooLong",
    "MgrRegisterFailed",
    "MissingOperand",
    "MultipleArchitectures",
    "MultipleExtensions",
    "MultipleOutputExtensions",
    "MultipleTapeBlockCounts",
    "NestedTooDeep",
    "NoSourceFiles",
    "OpenReadFailed",
    "OpenWriteFailed",
    "TapeSizeNotNumeric",
    "TapeSizeZero",
    "TapeTooLarge",
    "TooManyInstructions",
    "UnknownArch",
    "UnknownArg",
    "UnmatchedClose",
    "UnmatchedOpen",
    "WriteTooLarge",
    /* ICE divider */
    "ICE:AppendToNull",
    "ICE:ImmediateTooLarge",
    "ICE:InvalidIr",
    "ICE:InvalidJump",
    "ICE:MgrCloseUnmanagedFd",
    "ICE:MgrFreeUnmanagedPtr",
    "ICE:MgrReallocUnmanagedPtr",
    "ICE:MgrParamsTooLong",
    "ICE:MgrTooManyAllocs",
    "ICE:MgrTooManyOpens",
    /* AllocFailure divider */
    "Fatal:AllocFailure",
};

static out_mode err_mode = OUTMODE_NORMAL;

/* the only external access to the variables is through these functions. */
void quiet_mode(void) {
    if (err_mode == OUTMODE_NORMAL) err_mode = OUTMODE_QUIET;
}

void json_mode(void) {
    err_mode = OUTMODE_JSON;
}

/* avoid using err_to_json for this special case, as malloc may fail again,
 * causing a loop of failures to generate json error messages properly.
 * If puts or fputs call malloc internally, then there's nothing to be done. */
noreturn void alloc_err(void) {
    ERR_CALLBACK(BF_FATAL_ALLOC_FAILURE);
    switch (err_mode) {
        case OUTMODE_JSON:
            puts(
                "{\"errorId:\":\"Fatal:AllocFailure\","
                "\"message\":\"A call to malloc or realloc returned NULL.\"}"
            );
            fflush(stdout);
            break;
        case OUTMODE_NORMAL:
            fputs(
                "Error ALLOC_FAILED: A call to malloc or realloc returned "
                "NULL.\n",
                stderr
            );
            break;
        case OUTMODE_QUIET:
            break;
    }
    abort();
}

/* Write the escaped byte `c` to `dest`. `dest` MUST have at least 5 bytes
 * available.
 * If it's an ASCII character and is not a control character, it will be printed
 * as-is.
 * If it's one of '\n', '\r', '\f', '\t', or '\b', then it's backslash-escaped.
 * If it's another ASCII control character, it's  written in the form "\xNN",
 * where NN is its byte value, hex-encoded.
 * Returns the number of bytes written, not including the null terminator. */
nonnull_args static int char_esc(char c, char dest[5]) {
    /* escaped characters with single-letter ids */
    switch ((uchar)c) {
        case '\n':
            return sprintf(dest, "\\n");
        case '\r':
            return sprintf(dest, "\\r");
        case '\f':
            return sprintf(dest, "\\f");
        case '\t':
            return sprintf(dest, "\\t");
        case '\b':
            return sprintf(dest, "\\b");
        default:
            break;
    }
    /* ASCII control characters */
    if ((uchar)c < 040) return sprintf(dest, "\\x%02hhx", (uchar)c);
    /* default */
    return sprintf(dest, "%c", c);
}

/* U+FFFD REPLACEMENT CHARACTER, UTF-8 encoded. */
#define UNICODE_REPLACEMENT "\xef\xbf\xbd"

/* reads a UTF-8 encoded Unicode scalar value from `src`, escapes any characters
 * needing escaping, and replaces bytes that are invalid UTF-8 with the `U+FFFD`
 * REPLACEMENT CHARACTER codepoint.
 *
 * Returns a struct with 2 u8 members - `src_used` and `dst_used`, containing
 * the number of bytes read from `src` and written to `dst` (not including the
 * null terminator), respectively. */
static nonnull_args size_t
json_utf8_next(const char *restrict src, char dst[restrict 8]) {
    /* handle ASCII control characters as a special case first */
    if ((uchar)(*src) < 040) {
        char c;
        switch (*src) {
            case '\n':
                c = 'n';
                break;
            case '\r':
                c = 'r';
                break;
            case '\f':
                c = 'f';
                break;
            case '\t':
                c = 't';
                break;
            case '\b':
                c = 'b';
                break;
            default:
                sprintf(dst, "\\u%05hhx", (uchar)*src);
                return 1;
        }
        sprintf(dst, "\\%c", c);
        return 1;
    }
    if (src[0] == '\\') {
        dst[0] = dst[1] = '\\';
        dst[2] = '\0';
        return 1;
    }
    if (src[0] == '"') {
        dst[0] = '\\';
        dst[1] = '"';
        dst[2] = '\0';
        return 1;
    }
    if ((schar)src[0] >= 0) {
        dst[0] = src[0];
        dst[1] = '\0';
        return 1;
    }

    u8 seq_size;
    if (((uchar)src[0] & 0xe0) == 0xc0) {
        seq_size = 2;
    } else if (((uchar)src[0] & 0xf0) == 0xe0) {
        seq_size = 3;
    } else if (((uchar)src[0] & 0xf8) == 0xf0) {
        seq_size = 3;
    } else {
        goto invalid;
    }

    for (u8 i = 1; i < seq_size; i++) {
        if (((uchar)src[i] & 0xc0) != 0x80) goto invalid;
    }

    memcpy(dst, src, seq_size);
    dst[seq_size] = 0;
    return seq_size;

invalid:
    /* either there was not a continuation byte where required, or the leading
     * byte was invalid. */
    sprintf(dst, UNICODE_REPLACEMENT);
    return 1;
}

#define STRINGIFY(x) #x
#define METASTRINGIFY(x) STRINGIFY(x)
#define MAX_SIZE_STRLEN (sizeof(METASTRINGIFY(SIZE_MAX)))

static nonnull_ret char *err_to_json(const bf_comp_err err) {
    if (!err.msg) abort();
    sized_buf json_err = newbuf(BFC_CHUNK_SIZE);
    append_str(&json_err, "{\"errorId\": \"");
    append_str(&json_err, ERR_IDS[err.id]);
    append_str(&json_err, "\", ");

    char transfer[8];
    const char *p;
    if ((p = err.file)) {
        append_str(&json_err, "\"file\": \"");
        while (*p) {
            p += json_utf8_next(p, transfer);
            append_str(&json_err, transfer);
        }
        append_str(&json_err, "\", ");
    }
    if (err.has_location) {
        char loc_info[22 + (2 * MAX_SIZE_STRLEN)];
        size_t loc_info_sz = sprintf(
            loc_info,
            "\"line\": %ju, \"column\": %ju, ",
            (uintmax_t)err.line,
            (uintmax_t)err.col
        );
        append_obj(&json_err, loc_info, loc_info_sz);
    }

    if (err.has_instr) {
        append_str(&json_err, "\"instruction\": \"");
        if ((schar)err.instr > 040) {
            if (err.instr == '\\') {
                append_str(&json_err, "\\\\\"");
            } else if (err.instr == '"') {
                append_str(&json_err, "\\\"");
            } else {
                const char stringified[2] = {err.instr};
                append_str(&json_err, stringified);
            }
        } else if ((uchar)err.instr & 0x80) {
            append_str(&json_err, UNICODE_REPLACEMENT);
        } else {
            char escaped[8] = {'\\'};
            switch (err.instr) {
                case '\n':
                    escaped[1] = 'n';
                    break;
                case '\r':
                    escaped[1] = 'r';
                    break;
                case '\f':
                    escaped[1] = 'f';
                    break;
                case '\t':
                    escaped[1] = 't';
                    break;
                case '\b':
                    escaped[1] = 'b';
                    break;
                default:
                    sprintf(&escaped[1], "u%04hhx", (uchar)err.instr);
            }
            append_str(&json_err, escaped);
        }
        append_str(&json_err, "\", ");
    }

    append_str(&json_err, "\"message\": \"");
    p = err.msg;
    while (*p) {
        p += json_utf8_next(p, transfer);
        append_str(&json_err, transfer);
    }
    append_str(&json_err, "\"}");
    return json_err.buf;
}

static void json_eprint(bf_comp_err err) {
    char *p = err_to_json(err);
    puts(p);
    mgr_free(p);
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
            "Error %s in file %s%s: %s\n",
            ERR_IDS[err.id],
            err.file,
            extra_info,
            err.msg
        );
    } else {
        fprintf(
            stderr, "Error %s%s: %s\n", ERR_IDS[err.id], extra_info, err.msg
        );
    }
}

void display_err(const bf_comp_err e) {
    ERR_CALLBACK(e.id);
    switch (err_mode) {
        case OUTMODE_QUIET:
            break;
        case OUTMODE_NORMAL:
            normal_eprint(e);
            break;
        case OUTMODE_JSON:
            json_eprint(e);
            break;
    }
}

noreturn nonnull_args void internal_err(bf_err_id err_id, const char *msg) {
    ERR_CALLBACK(err_id);
    bf_comp_err e = {
        .msg = msg,
        .file = NULL,
        .has_instr = false,
        .has_location = false,
        .id = err_id
    };
    display_err(e);
    fflush(stdout);
    abort();
}
