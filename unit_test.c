/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

/* C99 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "testutils.h"
#include "types.h"
#include "util.h"

/* LLVM uses some obnoxiously long identifiers. This helps mitigate that */
#define INTEL_ASM LLVMDisassembler_Option_AsmPrinterVariant
#define HEX_IMMS LLVMDisassembler_Option_PrintImmHex
#define CREATE_DISASM(triple) LLVMCreateDisasm(triple, NULL, 0, NULL, NULL);
#define CREATE_DISASM_FEATURES(triple, features) \
    LLVMCreateDisasmCPUFeatures( \
        triple, "generic", features, NULL, 0, NULL, NULL \
    );
CU_pSuite register_util_tests(void);
CU_pSuite register_serialize_tests(void);

char *disassemble(disasm_ref ref, sized_buf bytes) {
    char disassembly[128];
    sized_buf output = newbuf(1024);
    size_t prev_sz;
    while ((prev_sz = bytes.sz)) {
        size_t used_sz = LLVMDisasmInstruction(
            ref, bytes.buf, bytes.sz, 0, disassembly, 128
        );
        if (!used_sz) {
            mgr_free(bytes.buf);
            mgr_free(output.buf);
            fprintf(
                stderr, "failed to decompile %ju bytes.\n", (uintmax_t)bytes.sz
            );
            return NULL;
        }
        memmove(bytes.buf, (char *)bytes.buf + used_sz, bytes.sz - used_sz);
        bytes.sz -= used_sz;
        append_obj(&output, disassembly, used_sz);
        append_obj(&output, "\n", 1);
    }
    char *terminator = sb_reserve(&output, 1);
    *terminator = 0;
    return output.buf;
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
    LLVMDisasmDispose(ARM64_DIS);
    LLVMDisasmDispose(RISCV64_DIS);
    LLVMDisasmDispose(S390X_DIS);
    LLVMDisasmDispose(X86_64_DIS);
}

int main(void) {
    register_mgr();
    if (atexit(llvm_cleanup)) {
        fputs("Failed to register llvm_cleanup with atexit\n", stderr);
        return EXIT_FAILURE;
    }
    llvm_init();

    if (CU_initialize_registry() != CUE_SUCCESS ||
        register_util_tests() == NULL || register_serialize_tests() == NULL) {
        return CU_get_error();
    }

    /* Run all tests using the console interface */
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();
    return CU_get_error();
}
