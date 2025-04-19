/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */
#ifndef BFC_ELF_H
#define BFC_ELF_H 1

/* BACKENDS `#define` a macro to the EM_* value for architecture */
#define ARCH_ARM64 183
#define ARCH_RISCV64 243
#define ARCH_S390X 22
#define ARCH_X86_64 62

#define BYTEORDER_LSB 1
#define BYTEORDER_MSB 2

#endif /* BFC_ELF_H */
