/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Utility functions for unit testing, and the unit testing entry point */

/* C99 */
#include <stdio.h>
#include <stdlib.h>

/* CUnit */
#include <CUnit/Basic.h>
#include <CUnit/CUnit.h>

/* libLLVM */
#include <llvm-c/Disassembler.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#define UNIT_TEST_C 1
/* internal */
#include "resource_mgr.h"
#include "types.h"
#include "unit_test.h"
#include "util.h"

/* LLVM uses some obnoxiously long identifiers. This helps mitigate that */
#define INTEL_ASM LLVMDisassembler_Option_AsmPrinterVariant
#define HEX_IMMS LLVMDisassembler_Option_PrintImmHex
#define CREATE_DISASM(triple) LLVMCreateDisasm(triple, NULL, 0, NULL, NULL);
#define CREATE_DISASM_FEATURES(triple, features) \
    LLVMCreateDisasmCPUFeatures( \
        triple, "generic", features, NULL, 0, NULL, NULL \
    );

bool disassemble(disasm_ref ref, sized_buf *bytes, sized_buf *disasm) {
    char disasm_insn[128];
    size_t prev_sz;
    disasm->sz = 0;
    while ((prev_sz = bytes->sz)) {
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

static void llvm_init(void) {
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllDisassemblers();
    ARM64_DIS = CREATE_DISASM("aarch64-linux-gnu");
    RISCV64_DIS = CREATE_DISASM_FEATURES("riscv64-linux-gnu", "+c");
    S390X_DIS = CREATE_DISASM_FEATURES("systemz-linux-gnu", "+high-word");
    X86_64_DIS = CREATE_DISASM("x86_64-linux-gnu");
    LLVMSetDisasmOptions(X86_64_DIS, INTEL_ASM);
    LLVMSetDisasmOptions(ARM64_DIS, HEX_IMMS);
    LLVMSetDisasmOptions(RISCV64_DIS, HEX_IMMS);
    LLVMSetDisasmOptions(S390X_DIS, HEX_IMMS);
    LLVMSetDisasmOptions(X86_64_DIS, HEX_IMMS);
}

static void llvm_cleanup(void) {
    static bool ran = false;
    if (ran) return;
    LLVMDisasmDispose(ARM64_DIS);
    LLVMDisasmDispose(RISCV64_DIS);
    LLVMDisasmDispose(S390X_DIS);
    LLVMDisasmDispose(X86_64_DIS);
    ran = true;
}

int main(void) {
    register_mgr();
    if (atexit(llvm_cleanup)) {
        fputs("Failed to register llvm_cleanup with atexit\n", stderr);
        return EXIT_FAILURE;
    }
    llvm_init();

    ERRORCHECKED(CU_initialize_registry());
    ERRORCHECKED(register_util_tests());
    ERRORCHECKED(register_serialize_tests());
    ERRORCHECKED(register_arm64_tests());
    ERRORCHECKED(register_riscv64_tests());
    ERRORCHECKED(register_s390x_tests());
    ERRORCHECKED(register_x86_64_tests());
    ERRORCHECKED(register_optimize_tests());

    /* Run all tests using the console interface */
    CU_basic_set_mode(CU_BRM_VERBOSE);

    ERRORCHECKED(CU_basic_run_tests());
    int ret = CU_get_number_of_tests_failed() ? EXIT_FAILURE : EXIT_SUCCESS;
    CU_cleanup_registry();
    llvm_cleanup();
    return ret;
}
