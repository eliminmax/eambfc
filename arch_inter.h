/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains typedefs of structs used to provide the interface to
 * architectures. */


#ifndef EAM_ARCH_INTER_H
#define EAM_ARCH_INTER_H 1
/* internal */
#include "types.h"

typedef const struct arch_registers {
    uint8_t sc_num;
    uint8_t arg1;
    uint8_t arg2;
    uint8_t arg3;
    uint8_t bf_ptr;
} arch_registers;

typedef const struct arch_sc_nums {
    int64_t read;
    int64_t write;
    int64_t exit;
} arch_sc_nums;

typedef const struct arch_funcs {
    /* register manipulation functions */
    bool (*set_reg)(uint8_t reg, int32_t imm, int fd, off_t *sz);
    bool (*reg_copy)(uint8_t dst, uint8_t src, int fd, off_t *sz);
    bool (*syscall)(int fd, off_t *sz);
    /* functions used to implement brainfuck instructions */
    /* [ */
    /* gets replaced once location of ] is known */
    bool (*nop_loop_open)(int fd, off_t *sz);
    /* replaces nop_loop_open. */
    bool (*jump_zero)(uint8_t reg, int32_t offset, int fd, off_t *sz);
    /* ] */
    bool (*jump_not_zero)(uint8_t reg, int32_t offset, int fd, off_t *sz);
    /* > */
    bool (*inc_reg)(uint8_t reg, int fd, off_t *sz);
    /* < */
    bool (*dec_reg)(uint8_t reg, int fd, off_t *sz);
    /* + */
    bool (*inc_byte)(uint8_t reg, int fd, off_t *sz);
    /* - */
    bool (*dec_byte)(uint8_t reg, int fd, off_t *sz);
    /* functions used for optimized instructions */
    bool (*add_reg)(uint8_t reg, int64_t imm, int fd, off_t *sz);
    bool (*sub_reg)(uint8_t reg, int64_t imm, int fd, off_t *sz);
    bool (*add_mem)(uint8_t reg, int8_t imm8, int fd, off_t *sz);
    bool (*sub_mem)(uint8_t reg, int8_t imm8, int fd, off_t *sz);
    bool (*zero_mem)(uint8_t reg, int fd, off_t *sz);
} arch_funcs;

typedef const struct arch_inter {
    arch_funcs FUNCS;
    arch_sc_nums SC_NUMS;
    arch_registers REGISTERS;
    uint16_t ELF_ARCH;
    unsigned char ELF_DATA;
} arch_inter;

const extern arch_inter X86_64_INTER;
#ifdef EAM_TARGET_ARM64
const extern arch_inter ARM64_INTER;
#endif /* EAM_TARGET_ARM64 */

# endif /* EAM_ARCH_INTER_H */
