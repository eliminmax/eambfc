/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#ifndef BFC_UNIT_TEST_H
#define BFC_UNIT_TEST_H 1

/* C99 */
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

/* libLLVM */
#include <llvm-c/Disassembler.h> /* IWYU pragma: export */

/* CUNIT */
#include <CUnit/CUnit.h> /* IWYU pragma: export */

/* internal */
#include "err.h"
#include "types.h"

#ifdef UNIT_TEST_C
#define TEST_GLOBAL(decl) decl
#else /* UNIT_TEST_C */
#define TEST_GLOBAL(decl) extern decl
#endif /* UNIT_TEST_C */

typedef LLVMDisasmContextRef disasm_ref;

/* __BACKENDS__ add a declaration of the disasm_ref here */
TEST_GLOBAL(disasm_ref ARM64_DIS);
TEST_GLOBAL(disasm_ref RISCV64_DIS);
TEST_GLOBAL(disasm_ref S390X_DIS);
TEST_GLOBAL(disasm_ref X86_64_DIS);

TEST_GLOBAL(bf_err_id current_err);

enum test_status {
    TEST_SET = -1,
    NOT_TESTING = 0,
    TEST_INTERCEPT = 1,
};

TEST_GLOBAL(enum test_status testing_err);

TEST_GLOBAL(jmp_buf etest_stack);

/* disassemble the contents of bytes, and return a sized_buf containing the
 * diassembly - instructions are separated by newlines, and the disassembly as a
 * whole is null-terminated. If any the provided bytes is unable to be fully
 * disassembled, it returns a sized_buf with sz and capacity set to zero, and
 * buf set to NULL.
 *
 * `bytes->sz` is set to zero by this process, but the allocation of
 * `bytes->buf` is left as-is, so it can be reused. */
bool disassemble(disasm_ref ref, sized_buf *bytes, sized_buf *disasm);

/* utility macro to test if a sized_buf contains the expected disassembly.
 * Clears both sb and dis, leaving the allocation behind for reuse if needed */
#define DISASM_TEST(code, dis, expected) \
    if (disassemble(REF, &code, &dis)) { \
        CU_ASSERT_STRING_EQUAL(dis.buf, expected); \
        if (strcmp(dis.buf, expected)) { \
            fprintf( \
                stderr, \
                "\n\n### EXPECTED ###\n%s\n\n### ACTUAL ###\n%s\n", \
                expected, \
                dis.buf \
            ); \
        } \
    } else { \
        CU_FAIL("Failed to decompile bytes!"); \
    }

/* utility macro to abort on CUnit error after running expr */
#define BF_ERRCHECKED(expr) \
    expr; \
    if (CU_get_error()) { \
        fprintf(stderr, "%s\n", CU_get_error_msg()); \
        exit(EXIT_FAILURE); \
    }

/* utility macro to set up a CUnit suite with the current file name */
#define INIT_SUITE(suite_var) \
    BF_ERRCHECKED(suite_var = CU_add_suite(__FILE__, NULL, NULL))

/* simple self-explanatory BF_ERRCHECKED wrapper around CU_ADD_TEST */
#define ADD_TEST(suite, test) BF_ERRCHECKED(CU_ADD_TEST(suite, test))

CU_pSuite register_util_tests(void);
CU_pSuite register_serialize_tests(void);
CU_pSuite register_optimize_tests(void);
CU_pSuite register_err_tests(void);
CU_pSuite register_compile_tests(void);

/* __BACKENDS__ add your test suite here */
CU_pSuite register_arm64_tests(void);
CU_pSuite register_riscv64_tests(void);
CU_pSuite register_s390x_tests(void);
CU_pSuite register_x86_64_tests(void);

#endif /* BFC_UNIT_TEST_H */
