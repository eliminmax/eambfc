/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */
/* C99 */
#include <stdlib.h>

/* CUnit */
#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>
CU_pSuite register_util_tests(void);

int main(void) {
    if (CU_initialize_registry() != CUE_SUCCESS ||
        register_util_tests() == NULL) {
        return CU_get_error();
    }
    /* Run all tests using the console interface */
    CU_basic_run_tests();
    CU_cleanup_registry();
    return CU_get_error();
}
