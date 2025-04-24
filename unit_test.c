/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Utility functions for unit testing, and the unit testing entry point */

#ifdef BFC_TEST
/* C99 */
#include <stdio.h>
#include <stdlib.h>

/* CUnit */
#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>

/* libLLVM */
#include <llvm-c/Core.h>
#include <llvm-c/Disassembler.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#define UNIT_TEST_C 1
/* internal */
#include <types.h>

#include "err.h"
#include "unit_test.h"
#include "util.h"

#define SET_DIS_OPTIONS(ref, opt) \
    if (!LLVMSetDisasmOptions(ref, opt)) exit(EXIT_FAILURE);

static bool LLVM_IS_INIT = false;

static void llvm_init(void) {
    if (LLVM_IS_INIT) return;
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllDisassemblers();

#define ARCH_DISASM(ref, triple, features) \
    ref = LLVMCreateDisasmCPUFeatures( \
        triple, "generic", features, NULL, 0, NULL, NULL \
    ); \
    if (!ref) { \
        fputs( \
            "Failed to initialize " #ref " with triple " #triple \
            " and features " #features ".\n", \
            stderr \
        ); \
        abort(); \
    }

#include "backends.h"
    /* use Intel assembly syntax - needs to be set before hex_imms, otherwise it
     * overwrites it */
    SET_DIS_OPTIONS(X86_64_DIS, LLVMDisassembler_Option_AsmPrinterVariant);

#define ARCH_DISASM(ref, ...) \
    SET_DIS_OPTIONS(ref, LLVMDisassembler_Option_PrintImmHex);
#include "backends.h"
    LLVM_IS_INIT = true;
}

bool disassemble(disasm_ref ref, sized_buf *bytes, sized_buf *disasm) {
    char disasm_insn[128];
    disasm->sz = 0;
    while (bytes->sz) {
        memset(disasm_insn, 0, 128);
        size_t used_sz = LLVMDisasmInstruction(
            ref, (u8 *)bytes->buf, bytes->sz, 0, disasm_insn, 128
        );
        if (!used_sz) return false;
        memmove(bytes->buf, bytes->buf + used_sz, bytes->sz - used_sz);
        bytes->sz -= used_sz;
        ufast_8 i;
        /* start at 1 to skip leading '\t' */
        /* Replace spaces with tabs. Don't need to explicitly check for the end
         * of the array because LLVMDisasmInstruction always null-terminates is
         * output. */
        for (i = 1; disasm_insn[i]; i++) {
            if (disasm_insn[i] == '\t') disasm_insn[i] = ' ';
        }
        /* replace null terminator with a newline */
        disasm_insn[i] = '\n';
        /* leave `i` as-is, as the 0-based indexing and the extra '\n' cancel
         * out, and the loop shouldn't be null-terminated yet. */
        append_obj(disasm, &disasm_insn[1], i);
    }
    /* null-terminate the output loop */
    char *terminator = sb_reserve(disasm, 1);
    *terminator = 0;
    return true;
}

static void llvm_cleanup(void) {
    if (!LLVM_IS_INIT) return;
#define ARCH_DISASM(ref, ...) LLVMDisasmDispose(ref);
#include "backends.h"
    LLVMShutdown();
    LLVM_IS_INIT = false;
}

int main(void) {
    if (atexit(llvm_cleanup)) {
        fputs("Failed to register llvm_cleanup with atexit\n", stderr);
        return EXIT_FAILURE;
    }
    llvm_init();
    quiet_mode();

    BF_ERRCHECKED(CU_initialize_registry());
    BF_ERRCHECKED(register_optimize_tests());
    BF_ERRCHECKED(register_util_tests());
    BF_ERRCHECKED(register_serialize_tests());
    BF_ERRCHECKED(register_err_tests());
    BF_ERRCHECKED(register_compile_tests());

#define ARCH_TEST_REGISTER(func) BF_ERRCHECKED(func());
#include "backends.h"

    testing_err = NOT_TESTING;
    /* Run all tests using the console interface */
    CU_basic_set_mode(CU_BRM_VERBOSE);

    BF_ERRCHECKED(CU_basic_run_tests());
    int ret = CU_get_number_of_tests_failed() ? EXIT_FAILURE : EXIT_SUCCESS;
    CU_cleanup_registry();
    llvm_cleanup();
    return ret;
}
#endif /* BFC_TEST */
