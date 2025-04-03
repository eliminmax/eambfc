/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */

/* C99 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* POSIX */
#include <unistd.h>
/* internal */
#include "attributes.h"
#include "config.h"
#include "err.h"
#include "types.h"
#include "util.h"

extern inline void *checked_malloc(size_t size);
extern inline void *checked_realloc(void *ptr, size_t size);
extern inline u8 trailing_0s(u64 val);
extern inline size_t chunk_pad(size_t nbytes);
extern inline bool bit_fits(i64 val, u8 bits);
extern inline sized_buf newbuf(size_t sz);
extern inline i64 sign_extend(i64 val, u8 nbits);
extern inline void append_str(
    sized_buf *restrict dst, const char *restrict str
);

nonnull_args bool write_obj(
    int fd, const void *restrict buf, size_t ct, bf_comp_err *restrict err
) {
    while (ct > (size_t)SSIZE_MAX) {
        if (write(fd, buf, SSIZE_MAX) != SSIZE_MAX) goto fail;
        buf = (const char *)buf + SSIZE_MAX;
        ct -= SSIZE_MAX;
    }
    if (write(fd, buf, ct) != (ssize_t)ct) goto fail;
    return true;
fail:
    *err = basic_err(BF_ERR_FAILED_WRITE, "Failed to write to file");
    return false;
}

/* reserve `nbytes` bytes at the end of `dst`, and returns a pointer to the
 * beginning of them - it's assumed that the caller will populate them, so the
 * `sb->sz` will be increased by `nbytes` */
nonnull_ret void *sb_reserve(sized_buf *sb, size_t nbytes) {
    if (sb->buf == NULL) {
        size_t new_cap = chunk_pad(nbytes);
        sb->buf = checked_malloc(new_cap);
        sb->capacity = new_cap;
    }

    /* if more space is needed, ensure no overflow occurs when calculating new
     * space requirements, then allocate it. */
    if (sb->sz + nbytes < sb->sz) {
        display_err(basic_err(
            BF_ERR_BUF_TOO_LARGE, "appending bytes would cause overflow"
        ));
        fflush(stdout);
        abort();
    }
    if (sb->sz + nbytes > sb->capacity) {
        /* will reallocate with 0x1000 to 0x2000 bytes of extra space */
        size_t needed_cap = chunk_pad(sb->sz + nbytes);
        /* reallocate to new capacity */
        sb->buf = checked_realloc(sb->buf, needed_cap);
        sb->capacity = needed_cap;
    }
    void *ret = &sb->buf[sb->sz];
    sb->sz += nbytes;
    return ret;
}

/* Append `bytes` to `dst`. If there's not enough room in `dst`, it will
 * `realloc` `dst->buf`, so if it's not heap allocated, care must be taken to
 * ensure enough space is provided to fit `bytes`. */
nonnull_args void append_obj(
    sized_buf *restrict dst, const void *restrict bytes, size_t bytes_sz
) {
    if (dst->buf == NULL) {
        size_t new_cap = chunk_pad(bytes_sz);
        dst->sz = 0;
        dst->buf = checked_malloc(new_cap);
        dst->capacity = new_cap;
    }
    /* how much capacity is needed */
    size_t needed_cap = bytes_sz + dst->sz;
    if (needed_cap < dst->sz) {
        display_err(basic_err(
            BF_ERR_BUF_TOO_LARGE, "appending bytes would cause overflow"
        ));
        fflush(stdout);
        abort();
    }

    if (needed_cap > dst->capacity) {
        /* reallocate to new capacity */
        dst->buf = checked_realloc(dst->buf, chunk_pad(needed_cap));
        dst->capacity = needed_cap;
    }
    /* actually append the object now that prep work is done */
    memcpy(dst->buf + dst->sz, bytes, bytes_sz);
    dst->sz += bytes_sz;
}

/* Tries to read the contents in the file associated with `fd` into a new
 * `sized_buf`, returning a tagged union with a boolean `success` indicating
 * whether it succeeded or not, and union `data` with `err` on failure and `sb`
 * on success. Only use `data.err` on failure, and only use `data.sb` on
 * success. */
bool read_to_sized_buf(int fd, union read_result *result) {
    result->sb = newbuf(BFC_CHUNK_SIZE);
    char chunk[BFC_CHUNK_SIZE];
    ssize_t count;
    while ((count = read(fd, &chunk, BFC_CHUNK_SIZE))) {
        if (count >= 0) {
            append_obj(&result->sb, &chunk, count);
        } else {
            free(result->sb.buf);
            result->err = basic_err(
                BF_ERR_FAILED_READ, "Failed to read file into buffer"
            );
            return false;
        }
    }
    return true;
}

#ifdef BFC_TEST
/* POSIX */
#include <fcntl.h>
#include <unistd.h>

/* internal */
#include "unit_test.h"

static void bit_fits_test(void) {
    for (uint i = 1; i < 32; i++) {
        i64 tst_val = INT64_C(1) << i;
        CU_ASSERT(bit_fits(tst_val, i + 2));
        CU_ASSERT(!bit_fits(tst_val, i + 1));
        CU_ASSERT(bit_fits(-tst_val, i + 1));
        CU_ASSERT(!bit_fits(-tst_val, i));
        CU_ASSERT(bit_fits(tst_val - 1, i + 1));
    }
}

static void test_sign_extend(void) {
    CU_ASSERT_EQUAL(sign_extend(0xf, 4), -1);
    CU_ASSERT_EQUAL(sign_extend(0xe, 4), -2);
    CU_ASSERT_EQUAL(sign_extend(0xf, 5), 0xf);
    CU_ASSERT_EQUAL(sign_extend(0x1f, 5), -1);
    CU_ASSERT_EQUAL(sign_extend(1, 1), -1);
}

static void trailing_0s_test(void) {
    CU_ASSERT_EQUAL(trailing_0s(0), UINT8_MAX);
    for (uint i = 0; i < 32; i++) {
        CU_ASSERT_EQUAL(trailing_0s(UINT64_C(1) << i), i);
    }
}

CU_pSuite register_util_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, bit_fits_test);
    ADD_TEST(suite, trailing_0s_test);
    ADD_TEST(suite, test_sign_extend);
    return suite;
}
#endif /* BFC_TEST */
