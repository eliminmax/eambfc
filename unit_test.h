/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#ifndef BFC_UNIT_TEST_H
#define BFC_UNIT_TEST_H 1

/* C99 */
#include <limits.h>
#include <stdio.h> /* IWYU pragma: export */
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
#define DISASM_TEST(ref, code, dis, expected) \
    if (disassemble(ref, &code, &dis)) { \
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

#define ERRORCHECKED(expr) \
    expr; \
    if (CU_get_error()) { \
        fprintf(stderr, "%s\n", CU_get_error_msg()); \
        exit(EXIT_FAILURE); \
    }

#define INIT_SUITE(suite_var) \
    suite_var = CU_add_suite(__BASE_FILE__, NULL, NULL); \
    if (suite_var == NULL) return NULL

#define ADD_TEST(suite, test) ERRORCHECKED(CU_ADD_TEST(suite, test))

CU_pSuite register_util_tests(void);
CU_pSuite register_serialize_tests(void);
CU_pSuite register_arm64_tests(void);
CU_pSuite register_riscv64_tests(void);
CU_pSuite register_s390x_tests(void);
CU_pSuite register_optimize_tests(void);

#endif /* BFC_UNIT_TEST_H */
