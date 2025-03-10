/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#ifndef BFC_UNIT_TEST_H
#define BFC_UNIT_TEST_H 1
/* C99 */
#include <limits.h>

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

/* disassemble the contents of bytes, and return a sized_buf containing the
 * diassembly - instructions are separated by newlines, and the disassembly as a
 * whole is null-terminated. If any bytes within sized_buf are unable to be
 * disassembled, it returns a sized_buf with sz and capacity set to zero, and
 * buf set to NULL.
 *
 * Note that `bytes.buf` will be `mgr_free`d no matter the outcome. */
sized_buf disassemble(disasm_ref ref, sized_buf bytes);

#endif /* BFC_UNIT_TEST_H */
