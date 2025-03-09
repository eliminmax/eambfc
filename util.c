/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Miscellaneous utility functions used throughout the eambfc codebase. */

/* C99 */
#include <limits.h>
#include <string.h>
/* POSIX */
#include <unistd.h>
/* internal */
#include "attributes.h"
#include "err.h"
#include "resource_mgr.h"
#include "types.h"

#define BFC_UTIL_C
#include "util.h"

/* Wrapper around write.3POSIX that returns true if all bytes were written, and
 * prints an error and returns false otherwise or if ct is too large to
 * validate. */
nonnull_args bool write_obj(
    int fd, const void *buf, size_t ct, const char *out_name
) {
    if (ct > SSIZE_MAX) {
        display_err((bf_comp_err){
            .id = BF_ERR_WRITE_TOO_LARGE,
            .msg =
                "Didn't write because write is too large to properly validate.",
            .has_location = false,
            .has_instr = false,
            .file = out_name,
        });
        return false;
    }
    ssize_t written = write(fd, buf, ct);
    if (written != (ssize_t)ct) {
        display_err((bf_comp_err){
            .id = BF_ERR_FAILED_WRITE,
            .msg = "Failed to write to file",
            .has_location = false,
            .has_instr = false,
            .file = out_name,
        });
        return false;
    }
    return true;
}

/* reserve nbytes bytes at the end of dst, and returns a pointer to the
 * beginning of them - it's assumed that the caller will populate them, so the
 * sized_buf will consider them used */
nonnull_ret void *sb_reserve(sized_buf *sb, size_t nbytes) {
    if (sb->buf == NULL) {
        internal_err(
            BF_ICE_APPEND_TO_NULL, "sb_reserve called with dst->buf set to NULL"
        );
        /* will never return, as internal_err calls exit(EXIT_FAILURE) */
    }
    /* if more space is needed, ensure no overflow occurs when calculating new
     * space requirements, then allocate it. */
    if (sb->sz + nbytes > sb->capacity) {
        /* will reallocate with 0x1000 to 0x2000 bytes of extra space */
        size_t needed_cap = (sb->sz + nbytes + 0x1000) & (~0xfff);
        if (needed_cap < sb->capacity) {
            mgr_free(sb->buf);
            sb->capacity = 0;
            sb->sz = 0;
            sb->buf = NULL;
            alloc_err();
        }
        /* reallocate to new capacity */
        sb->buf = mgr_realloc(sb->buf, needed_cap);
        sb->capacity = needed_cap;
    }
    sb->sz += nbytes;
    return (char *)sb->buf + sb->sz - nbytes;
}

/* Append bytes to dst, handling reallocs as needed.
 * Assumes that dst has been allocated with resource_mgr. */
nonnull_args bool append_obj(
    sized_buf *dst, const void *bytes, size_t bytes_sz
) {
    /* if more space is needed, ensure no overflow occurs when calculating new
     * space requirements, then allocate it.
     *
     * Make sure to leave 8 KiB shy of SIZE_MAX available - it will never
     * get anywhere near that high in any realisitic scenario, and the extra
     * space simplifies overflow checking logic. Besides, any sensible malloc
     * implementation will be returning NULL well before this is relevant. */
    if ((bytes_sz > (SIZE_MAX - 0x8000)) ||
        (dst->capacity > (SIZE_MAX - (bytes_sz + 0x8000)))) {
        mgr_free(dst->buf);
        dst->capacity = 0;
        dst->sz = 0;
        dst->buf = NULL;
        alloc_err();
    }

    if (dst->buf == NULL) {
        internal_err(
            BF_ICE_APPEND_TO_NULL, "append_obj called with dst->buf set to NULL"
        );
    }
    /* how much capacity is needed */
    size_t needed_cap = bytes_sz + dst->sz;
    /* if needed_cap isn't a multiple of 4 KiB in size, pad it out -
     * most usage of this function is going to be for small objects, so the
     * number of reallocations is vastly reduced that way.
     *
     * Because the previous check established that there's at least 8 KiB of
     * padding available, this is guaranteed not to overflow. */
    if (needed_cap & 0xfff) needed_cap = (needed_cap + 0x1000) & (~0xfff);

    if (needed_cap > dst->capacity) {
        /* reallocate to new capacity */
        dst->buf = mgr_realloc(dst->buf, needed_cap);
        dst->capacity = needed_cap;
    }
    /* actually append the object now that prep work is done */
    memcpy((char *)(dst->buf) + dst->sz, bytes, bytes_sz);
    dst->sz += bytes_sz;
    return true;
}

/* Reads the contents of fd into sb. If a read error occurs, frees what's
 * already been read, and sets sb to {0, 0, NULL}. */
sized_buf read_to_sized_buf(int fd, const char *in_name) {
    sized_buf sb = newbuf(4096);
    /* read into sb in 4096-byte chunks */
    char chunk[4096];
    ssize_t count;
    while ((count = read(fd, &chunk, 4096))) {
        if (count >= 0) {
            append_obj(&sb, &chunk, count);
        } else {
            display_err((bf_comp_err){
                .file = in_name,
                .id = BF_ERR_FAILED_READ,
                .msg = "Failed to read file into buffer",
                .has_instr = false,
                .has_location = false,
            });
            mgr_free(sb.buf);
            sb.sz = 0;
            sb.capacity = 0;
            sb.buf = NULL;
        }
    }
    return sb;
}

#ifdef BFC_TEST
/* CUnit */
#include <CUnit/CUnit.h>

/* POSIX */
#include <fcntl.h>
#include <unistd.h>

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
    for (uint i = 0; i < 32; i++) CU_ASSERT_EQUAL(trailing_0s(1 << i), i);
}

CU_pSuite register_util_tests(void) {
    CU_pSuite suite = CU_add_suite("util", NULL, NULL);
    if (suite == NULL) return NULL;
    CU_ADD_TEST(suite, bit_fits_test);
    CU_ADD_TEST(suite, trailing_0s_test);
    CU_ADD_TEST(suite, test_sign_extend);
    return suite;
}
#endif /* BFC_TEST */
