/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains functions that encode x86_64 machine instructions and
 * write them to open file descriptors. Each returns a boolean value indicating
 * whether or not the write was successful, and take a pointer to an off_t sz,
 * to which they add the number of bytes written. */
#ifndef EAM_INSTR_ENCODERS_H
#define EAM_INSTR_ENCODERS_H 1
/* POSIX */
#include <unistd.h> /* off_t */
/* internal */
#include "config.h" /* TARGET_ARCH, EM_* */
#include "types.h" /* bool, int*_t, uint8_t */

#if TARGET_ARCH == EM_X86_64
/* internal */
#include "x86_64_constants.h" /* REG_*, JUMP_SIZE, ARCH_*, SYSCALL_* */
#endif /* TARGET_ARCH */

/* used for making system calls, setup, and other miscellaneous things */
bool bfc_set_reg(uint8_t reg, int32_t imm, int fd, off_t *sz);
bool bfc_reg_copy(uint8_t dst, uint8_t src, int fd, off_t *sz);
bool bfc_syscall(int fd, off_t *sz);

/* fill space that will have `[` instruction with nop instructions until the
 * offset value is known. */
bool bfc_nop_loop_open(int fd, off_t *sz);

/* ] */
bool bfc_jump_not_zero(uint8_t reg, int32_t offset, int fd, off_t *sz);
/* [ */
bool bfc_jump_zero(uint8_t reg, int32_t offset, int fd, off_t *sz);
/* > */
bool bfc_inc_reg(uint8_t reg, int fd, off_t *sz);
/* < */
bool bfc_dec_reg(uint8_t reg, int fd, off_t *sz);
/* + */
bool bfc_inc_byte(uint8_t reg, int fd, off_t *sz);
/* - */
bool bfc_dec_byte(uint8_t reg, int fd, off_t *sz);

/* machine code for extra instructions from eambfc-ir */
/* } */
bool bfc_add_reg(uint8_t reg, int64_t imm, int fd, off_t *sz);
/* { */
bool bfc_sub_reg(uint8_t reg, int64_t imm, int fd, off_t *sz);
/* # */
bool bfc_add_mem(uint8_t reg, int8_t imm8, int fd, off_t *sz);
/* = */
bool bfc_sub_mem(uint8_t reg, int8_t imm8, int fd, off_t *sz);
/* @ */
bool bfc_zero_mem(uint8_t reg, int fd, off_t *sz);
#endif /* EAM_INSTR_ENCODERS_H */
