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
#include "types.h"

#ifdef UNIT_TEST_C
#define unit_extern
#else /* UNIT_TEST_C */
#define unit_extern extern
#endif /* UNIT_TEST_C */

typedef LLVMDisasmContextRef disasm_ref;

unit_extern disasm_ref ARM64_DIS;
unit_extern disasm_ref RISCV64_DIS;
unit_extern disasm_ref S390X_DIS;
unit_extern disasm_ref X86_64_DIS;

unit_extern bool testing_err;
unit_extern jmp_buf etest_stack;

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
#define ERRORCHECKED(expr) \
    expr; \
    if (CU_get_error()) { \
        fprintf(stderr, "%s\n", CU_get_error_msg()); \
        exit(EXIT_FAILURE); \
    }

/* utility macro to set up a CUnit suite with the current file name */
#define INIT_SUITE(suite_var) \
    ERRORCHECKED(suite_var = CU_add_suite(__BASE_FILE__, NULL, NULL))

/* simple self-explanatory ERRORCHECKED wrapper around CU_ADD_TEST */
#define ADD_TEST(suite, test) ERRORCHECKED(CU_ADD_TEST(suite, test))

/* boilerplate for use of setjmp to test if the right error is hit.
 * will result in undefined behavior if it's in a function that doesn't result
 * in one of `internal_err`, `alloc_err`, or `display_err` being called later */
#define EXPECT_BF_ERR(eid) \
    do { \
        testing_err = true; \
        int returned_err; \
        if ((returned_err = setjmp(etest_stack))) { \
            CU_ASSERT_EQUAL(eid << 0 | 1, returned_err); \
        } \
        testing_err = false; \
        return; \
    } while (0);

CU_pSuite register_util_tests(void);
CU_pSuite register_serialize_tests(void);
CU_pSuite register_optimize_tests(void);

/* __BACKENDS__ add your test suite here */
CU_pSuite register_arm64_tests(void);
CU_pSuite register_riscv64_tests(void);
CU_pSuite register_s390x_tests(void);
CU_pSuite register_x86_64_tests(void);

#endif /* BFC_UNIT_TEST_H */
