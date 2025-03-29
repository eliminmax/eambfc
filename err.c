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
                "{\"errorId\":\"Fatal:AllocFailure\","
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
    switch (c) {
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
        case '\a':
            return sprintf(dest, "\\a");
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
        /* size of template - 7 + maximum space needed for 2 size_t */
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
        /* because 0 is not a valid utf-8 continuation byte, json_utf8_next
         * will only consume the first byte, and will both JSON-escape and
         * utf8-sanitize it. */
        json_utf8_next((const char[]){err.instr, 0}, transfer);
        append_str(&json_err, transfer);
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

static nonnull_ret char *err_to_string(const bf_comp_err err) {
    if (!err.msg) abort();
    sized_buf err_str = newbuf(BFC_CHUNK_SIZE);
    append_str(&err_str, "Error ");
    append_str(&err_str, ERR_IDS[err.id]);
    if (err.file) {
        append_str(&err_str, " in file ");
        append_str(&err_str, err.file);
    }
    if (err.has_location) {
        /* size of template - 7 + maximum space needed for 2 size_t */
        char loc_info[18 + (2 * MAX_SIZE_STRLEN)];
        size_t loc_info_sz = sprintf(
            loc_info,
            " at line %ju, column %ju",
            (uintmax_t)err.line,
            (uintmax_t)err.col
        );
        append_obj(&err_str, loc_info, loc_info_sz);
    }
    if (err.has_instr) {
        char instr[5];
        append_str(&err_str, " (instruction ");
        char_esc(err.instr, instr);
        append_str(&err_str, instr);
        append_str(&err_str, ")");
    }
    append_str(&err_str, ": ");
    append_str(&err_str, err.msg);
    append_str(&err_str, "\n");
    return err_str.buf;
}

void display_err(const bf_comp_err e) {
    ERR_CALLBACK(e.id);
    if (e.msg == NULL) abort();
    char *errmsg;
    switch (err_mode) {
        case OUTMODE_QUIET:
            return;
        case OUTMODE_NORMAL:
            errmsg = err_to_string(e);
            fputs(errmsg, stderr);
            break;
        case OUTMODE_JSON:
            errmsg = err_to_json(e);
            puts(errmsg);
            break;
        default:

#if defined __GNUC__ && defined __has_builtin

#if __has_builtin(__builtin_unreachable)
            __builtin_unreachable();
#else /* __has_builtin(__builtin_unreachable) */
            abort();
#endif /* __has_builtin(__builtin_unreachable) */

#else /* defined __GNUC__ && defined __has_builtin */
            abort();
#endif /* defined __GNUC__ && defined __has_builtin */
    }
    free(errmsg);
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
