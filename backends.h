/* SPDX-FileCopyrightText: 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is inspired by a pattern in LLVM's source code, where redundant
 * backend information is in single files, formatted in a way that, depending on
 * macro definintions, would allow the same information to be interpreted
 * differently when #included in different places
 *
 * It's the kind of ugly "clever hack" that should be viewed with suspicion, but
 * it greatly simplifies the process of adding new backends, so I think it's
 * worth it. */
#include <config.h>

#ifndef ARCH_ID
#define ARCH_ID(target_macro, id)
#endif /* ARCH_ID */

#ifndef ARCH_INTER
#define ARCH_INTER(inter, name, /* aliases: */...)
#endif /* ARCH_INTER */

#ifndef ARCH_DISASM
#define ARCH_DISASM(ref, llvm_triple, cpu_features)
#endif /* ARCH_DISASM */

#ifndef ARCH_TEST_REGISTER
#define ARCH_TEST_REGISTER(register_func)
#endif

ARCH_ID(BFC_TARGET_ARM64, arm64)
#if BFC_TARGET_ARM64
ARCH_INTER(ARM64_INTER, "arm64", "aarch64")
ARCH_DISASM(ARM64_DIS, "aarch64-linux-gnu", "")
ARCH_TEST_REGISTER(register_arm64_tests)
#endif /* BFC_TARGET_ARM64 */

ARCH_ID(BFC_TARGET_RISCV64, riscv64)
#if BFC_TARGET_RISCV64
ARCH_INTER(RISCV64_INTER, "riscv64", "riscv")
ARCH_DISASM(RISCV64_DIS, "riscv64-linux-gnu", "+c")
ARCH_TEST_REGISTER(register_riscv64_tests)
#endif /* BFC_TARGET_RISCV64 */

ARCH_ID(BFC_TARGET_S390X, s390x)
#if BFC_TARGET_S390X
ARCH_INTER(S390X_INTER, "s390x", "s390", "z/architecture")
ARCH_DISASM(S390X_DIS, "systemz-linux-gnu", "+high-word")
ARCH_TEST_REGISTER(register_s390x_tests)
#endif /* BFC_TARGET_S390X */

ARCH_ID(BFC_TARGET_X86_64, x86_64)
#if BFC_TARGET_X86_64
ARCH_INTER(X86_64_INTER, "x86_64", "x64", "amd64", "x86-64")
ARCH_DISASM(X86_64_DIS, "x86_64-linux-gnu", "")
ARCH_TEST_REGISTER(register_x86_64_tests)
#endif /* BFC_TARGET_X86_64 */

#undef ARCH_ID
#undef ARCH_INTER
#undef ARCH_DISASM
#undef ARCH_TEST_REGISTER
