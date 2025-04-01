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
    switch (testing_err) { \
        case TEST_INTERCEPT: \
            longjmp(etest_stack, ((e_id) << 1) | 1); \
        case TEST_SET: \
            current_err = e_id; \
            break; \
        default: \
            break; \
    }

#else /* BFC_TEST */
#define ERR_CALLBACK(e_id) (void)0
#endif /* BFC_TEST */

extern inline bf_comp_err basic_err(bf_err_id id, const char *msg);

/* to keep the order consistent with the bf_err_id enum, do normal errors, then
 * ICEs, and finally "Fatal:AllocFailure", with each group sorted
 * alphabetically, except for ICE:InvalidErrId, which must come at the end and
 * has no string equivalent. */
const char *const ERR_IDS[] = {
    "InvalidErrorId",
    "BadSourceExtension",
    "BufferTooLarge",
    "FailedRead",
    "FailedWrite",
    "InputIsOutput",
    "JumpTooLong",
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
    "UnknownArch",
    "UnknownArg",
    "UnmatchedClose",
    "UnmatchedOpen",
    /* ICE divider */
    "ICE:AppendToNull",
    "ICE:ImmediateTooLarge",
    "ICE:InvalidIr",
    "ICE:InvalidJump",
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
                sprintf(dst, "\\u%04hhx", (uchar)*src);
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
    if (!err.msg.ref) abort();
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
            (uintmax_t)err.location.line,
            (uintmax_t)err.location.col
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
    p = err.msg.ref;
    while (*p) {
        p += json_utf8_next(p, transfer);
        append_str(&json_err, transfer);
    }
    append_str(&json_err, "\"}");
    return json_err.buf;
}

static nonnull_ret char *err_to_string(const bf_comp_err err) {
    if (!err.msg.ref) abort();
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
            (uintmax_t)err.location.line,
            (uintmax_t)err.location.col
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
    append_str(&err_str, err.msg.ref);
    append_str(&err_str, "\n");
    return err_str.buf;
}

void display_err(bf_comp_err e) {
    ERR_CALLBACK(e.id);
    if (!e.msg.ref) abort();
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
#if defined __GNUC__ && defined __has_builtin && !defined BFC_NOEXTENSIONS
#if __has_builtin(__builtin_unreachable)
            __builtin_unreachable();
#endif /* __has_builtin(__builtin_unreachable) */
#endif /* defined __GNUC__ && defined __has_builtin && ... */
            abort();
    }
    if (e.is_alloc) free(e.msg.alloc);
    free(errmsg);
}

noreturn nonnull_args void internal_err(bf_err_id err_id, const char *msg) {
    ERR_CALLBACK(err_id);
    display_err(basic_err(err_id, msg));
    fflush(stdout);
    abort();
}

#ifdef BFC_TEST
/* C99 */
#include <errno.h>
/* internal */
#include "unit_test.h"
/* json-c */
#include <json.h>

static bool err_eq(
    const bf_comp_err *restrict a, const bf_comp_err *restrict b
) {
    if (!(a->msg.ref && b->msg.ref)) return false;
    if (a->has_instr != b->has_instr) return false;
    if (a->has_instr && a->instr != b->instr) return false;
    if (a->has_location != b->has_location) return false;
    if (a->has_location && (a->location.line != b->location.line ||
                            a->location.col != b->location.col)) {
        return false;
    }
    if ((a->file == NULL) != (b->file == NULL)) return false;
    if (a->file && strcmp(a->file, b->file) != 0) return false;
    return (strcmp(a->msg.ref, b->msg.ref) == 0 && a->id == b->id);
}

static bf_err_id text_to_errid(const char *text) {
    for (size_t i = 0; i < (sizeof(ERR_IDS) / sizeof(const char *)); i++) {
        if (strcmp(ERR_IDS[i], text) == 0) return i;
    }
    return BF_NOT_ERR;
}

static bool check_err_json(const char *json_text, const bf_comp_err *expected) {
    struct json_object *err_jobj, *transfer;
    bool ret = false;

#define JSON_TYPE_CHECK(key, type) \
    if (json_object_get_type(transfer) != json_type_##type) { \
        fputs("\"" key "\" is not a \"" #type "\"!\n", stderr); \
        goto cleanup; \
    }

    err_jobj = json_tokener_parse(json_text);

    struct partial_comp_error {
        char *msg;
        char *file;
        size_t line;
        size_t col;
        bf_err_id id;
        char instr;
        bool has_instr   : 1;
        bool has_location: 1;
    } partial_err = {NULL, NULL, 0, 0, BF_NOT_ERR, 0, false, false};

    if (!json_object_object_get_ex(err_jobj, "message", &transfer)) {
        fputs("Could not get \"message\"!\n", stderr);
        goto cleanup;
    }

    JSON_TYPE_CHECK("message", string);

    partial_err.msg = checked_malloc(json_object_get_string_len(transfer) + 1);
    strcpy(partial_err.msg, json_object_get_string(transfer));

    if (!json_object_object_get_ex(err_jobj, "errorId", &transfer)) {
        fputs("Could not get \"errorId\"!\n", stderr);
        goto cleanup;
    }

    const char *copystr = json_object_get_string(transfer);

    if (!copystr) {
        fputs("Could not get \"errorId\" as string!\n", stderr);
        goto cleanup;
    }

    partial_err.id = text_to_errid(copystr);
    if (partial_err.id == BF_NOT_ERR) {
        fprintf(
            stderr, "Unrecognized value for \"errorId\": \"%s\"!\n", copystr
        );
        goto cleanup;
    }

    if (json_object_object_get_ex(err_jobj, "file", &transfer)) {
        JSON_TYPE_CHECK("file", string);
        partial_err.file =
            checked_malloc(json_object_get_string_len(transfer) + 1);
        strcpy(partial_err.file, json_object_get_string(transfer));
    }

    if (json_object_object_get_ex(err_jobj, "line", &transfer)) {
        JSON_TYPE_CHECK("instr", int);
        errno = 0;
        size_t line = json_object_get_int64(transfer);
        if (errno) {
            fputs("failed to get value of \"line\"!\n", stderr);
            goto cleanup;
        }
        if (!json_object_object_get_ex(err_jobj, "column", &transfer)) {
            fputs("line without column provided!\n", stderr);
            goto cleanup;
        }
        errno = 0;
        size_t col = json_object_get_int64(transfer);
        if (errno) {
            fputs("failed to get value of \"column\"!\n", stderr);
            goto cleanup;
        }
        partial_err.has_location = 1;
        partial_err.col = col;
        partial_err.line = line;
    } else if (json_object_object_get_ex(err_jobj, "column", &transfer)) {
        fputs("column without line provided!\n", stderr);
        goto cleanup;
    }
    if (json_object_object_get_ex(err_jobj, "instr", &transfer)) {
        JSON_TYPE_CHECK("instr", string);
        copystr = json_object_get_string(transfer);
        size_t len;
        if ((len = strlen(copystr)) != 1) {
            fprintf(
                stderr,
                "length of obj[\"instr\"] is %ju, not 0\n",
                (uintmax_t)len
            );
            goto cleanup;
        }
        partial_err.has_instr = true;
        partial_err.instr = *copystr;
    }

    ret = err_eq(
        expected,
        &(bf_comp_err){
            .msg.ref = partial_err.msg,
            .file = partial_err.file,
            .location.line = partial_err.line,
            .location.col = partial_err.col,
            .id = partial_err.id,
            .instr = partial_err.instr,
            .has_instr = partial_err.has_instr,
            .has_location = partial_err.has_location,
        }
    );

cleanup:
    if (partial_err.msg) free(partial_err.msg);
    if (partial_err.file) free(partial_err.file);
    json_object_put(err_jobj);
    return ret;
#undef JSON_TYPE_CHECK
}

static void json_sanity_test(void) {
    const char *err_text =
        "{\"errorId\":\"Fatal:AllocFailure\","
        "\"message\":\"A call to malloc or realloc returned NULL.\"}";
    bf_comp_err expected = {
        .msg.ref = "A call to malloc or realloc returned NULL.",
        .id = BF_FATAL_ALLOC_FAILURE,
        .has_instr = false,
        .has_location = false,
    };
    CU_ASSERT(check_err_json(err_text, &expected));
}

static void non_utf8_filename(void) {
    bf_comp_err expected = {
        .msg.ref = "Some error occurred in a file with a non-UTF8 name.",
        .id = BF_ERR_BAD_EXTENSION,
        .has_instr = false,
        .has_location = false,
        .file = "somefile.b" UNICODE_REPLACEMENT "f"
    };
    bf_comp_err test_err;
    memcpy(&test_err, &expected, sizeof(bf_comp_err));
    test_err.file =
        "somefile.b"
        "\xee"
        "f";
    char *err_json = err_to_json(test_err);
    CU_ASSERT(check_err_json(err_json, &expected));
    free(err_json);
}

static void json_escape_test(void) {
    char transfer[8];
    size_t read_sz;
    sized_buf output = newbuf(128);

    for (char i = 0; i < 0x10; i++) {
        CU_ASSERT_EQUAL(json_utf8_next((char[2]){i, 0}, transfer), 1);
        append_str(&output, transfer);
    }
    CU_ASSERT_STRING_EQUAL(
        output.buf,
        "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007"
        "\\b\\t\\n\\u000b\\f\\r\\u000e\\u000f"
    );
    output.sz = 0;

    CU_ASSERT_EQUAL(json_utf8_next("\"", transfer), 1);
    append_str(&output, transfer);
    CU_ASSERT_EQUAL(json_utf8_next("\'", transfer), 1);
    append_str(&output, transfer);
    CU_ASSERT_EQUAL(json_utf8_next("\\", transfer), 1);
    append_str(&output, transfer);
    CU_ASSERT_STRING_EQUAL(output.buf, "\\\"'\\\\");
    output.sz = 0;

    CU_ASSERT_EQUAL(json_utf8_next(" ", transfer), 1);
    append_str(&output, transfer);
    CU_ASSERT_EQUAL(json_utf8_next("\x90", transfer), 1);
    append_str(&output, transfer);
    CU_ASSERT_STRING_EQUAL(output.buf, " " UNICODE_REPLACEMENT);
    output.sz = 0;

    CU_ASSERT_EQUAL(json_utf8_next(UNICODE_REPLACEMENT, transfer), 3);
    append_str(&output, transfer);
    CU_ASSERT_EQUAL(output.sz, 3);
    CU_ASSERT_STRING_EQUAL(output.buf, UNICODE_REPLACEMENT);
    output.sz = 0;

    const char *readptr = "Hello, world!\n";
    while (*readptr) {
        read_sz = json_utf8_next(readptr, transfer);
        CU_ASSERT_EQUAL(read_sz, 1);
        append_str(&output, transfer);
        readptr += read_sz;
    }
    CU_ASSERT_STRING_EQUAL(output.buf, "Hello, world!\\n");
    free(output.buf);
}

CU_pSuite register_err_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, json_sanity_test);
    ADD_TEST(suite, non_utf8_filename);
    ADD_TEST(suite, json_escape_test);
    return suite;
}

#endif
