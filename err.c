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
#include "config.h"
#include "err.h"
#include "types.h"

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

/* avoid using json_str for this special case, as malloc may fail again,
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
    exit(EXIT_FAILURE);
}

/* Write the escaped byte `c` to `dest`. `dest` MUST have at least 5 bytes
 * available.
 * If it's an ASCII character and is not a control character, it will be printed
 * as-is.
 * If it's one of '\n', '\r', '\f', '\t', or '\b', then it's backslash-escaped.
 * If it's another ASCII control character, it's  written in the form "\xNN",
 * where NN is its byte value, hex-encoded.
 * Returns the number of bytes written, not including the null terminator. */
nonnull_args static int char_esc(char c, char *dest) {
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
    if ((uchar)c < 040) { return sprintf(dest, "\\x%02hhx", (uchar)c); }
    /* default */
    return sprintf(dest, "%c", c);
}

typedef struct {
    u8 src_used;
    u8 dst_used;
} json_trans;

/* reads a UTF-8 encoded Unicode scalar value from `src`, escapes any characters
 * needing escaping, and replaces bytes that are invalid UTF-8 with the `U+FFFD`
 * REPLACEMENT CHARACTER codepoint.
 *
 * Returns a struct with 2 u8 members - `src_used` and `dst_used`, containing
 * the number of bytes read from `src` and written to `dst` (not including the
 * null terminator), respectively.
 *
 * `dst` must have at least 7 bytes available for writing to avoid buffer
 * overflow. */
static nonnull_args json_trans
json_utf8_next(const char *restrict src, char *restrict dst) {
    /* handle ASCII control characters as a special case first */
    json_trans ret;
    if ((uchar)(*src) < 040) {
        ret.src_used = 1;
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
                ret.dst_used = sprintf(dst, "\\u%05hhx", (uchar)*src);
                return ret;
        }
        ret.dst_used = sprintf(dst, "\\%c", c);
        return ret;
    }
    if ((schar)src[0] >= 0) {
        dst[0] = src[0];
        dst[1] = '\0';
        ret.src_used = ret.dst_used = 1;
        return ret;
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
    ret.src_used = ret.dst_used = seq_size;
    return ret;

invalid:
    /* either there was not a continuation byte where required, or the leading
     * byte was invalid. */
    /* U+FFFD REPLACEMENT CHARACTER, UTF-8 encoded. */
    return (json_trans){.src_used = 1, .dst_used = sprintf(dst, "\xef\xbf\xbd")
    };
}

/* return a pointer to a JSON-escaped version of the input string
 * calling function is responsible for freeing it */
nonnull_ret nonnull_args static char *json_str(const char *str) {
    size_t bufsz = BFC_CHUNK_SIZE;
    const char *p = str;
    char *escaped = malloc(bufsz);
    if (escaped == NULL) alloc_err();
    size_t index = 0;
    while (*p) {
        json_trans transfer = json_utf8_next(p, &escaped[index]);
        index += transfer.dst_used;
        /* If less than 8 chars are left before overflow, allocate more space */
        if (index > bufsz - 8) {
            bufsz += BFC_CHUNK_SIZE;
            char *reallocator = realloc(escaped, bufsz);
            if (reallocator == NULL) {
                free(escaped);
                alloc_err();
            }
            escaped = reallocator;
        }
        p += transfer.src_used;
    }
    escaped[index] = 0;
    return escaped;
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
                case '\\':
                    fputs("\"instruction\": \"\\\\\", ", stdout);
                    break;
                case '"':
                    fputs("\"instruction\": \"\\\\\", ", stdout);
                    break;
                default:
                    printf("\"instruction\": \"%c\", ", err.instr);
            }
        } else if ((uchar)err.instr & 0x80) {
            printf("\"instruction\": \"�\"");
        } else {
            switch (err.instr) {
                case '\n':
                    fputs("\"instruction\": \"\\n\", ", stdout);
                    break;
                case '\r':
                    fputs("\"instruction\": \"\\r\", ", stdout);
                    break;
                case '\f':
                    fputs("\"instruction\": \"\\f\", ", stdout);
                    break;
                case '\t':
                    fputs("\"instruction\": \"\\t\", ", stdout);
                    break;
                case '\b':
                    fputs("\"instruction\": \"\\b\", ", stdout);
                    break;
                default:
                    printf(
                        "\"instruction\": \"\\u%04hhx\", ", (uchar)err.instr
                    );
            }
        }
    }
    printf("\"message\": \"%s\"}\n", msg);
    free(msg);
}

void display_err(bf_comp_err e) {
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
    exit(EXIT_FAILURE);
}
