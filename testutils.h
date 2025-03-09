/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#ifndef BFC_TESTUTILS_H
#define BFC_TESTUTILS_H 1

/* libLLVM */
#include <llvm-c/Disassembler.h>
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

char *disassemble(disasm_ref ref, sized_buf bytes);

#endif /* BFC_TESTUTILS_H */
