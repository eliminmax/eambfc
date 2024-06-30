/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains functions that encode x86_64 machine instructions and
 * write them to open file descriptors. Each returns a boolean value indicating
 * whether or not the write was successful, and take a pointer to an off_t sz,
 * to which they add the number of bytes written. */
#ifndef EAM_INSTRUCTION_ENCODERS_H
#define EAM_INSTRUCTION_ENCODERS_H 1
/* POSIX */
#include <unistd.h>
/* internal */
#include "types.h"

/* used for making system calls, setup, and other miscellaneous things */
bool bfc_set_reg(uint8_t reg, int32_t imm, int fd, off_t *sz);
bool bfc_reg_copy(uint8_t dst, uint8_t src, int fd, off_t *sz);
bool bfc_syscall(int fd, off_t *sz);

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
bool bfc_add_reg_imm8(uint8_t reg, int8_t imm8, int fd, off_t *sz);
/* { */
bool bfc_sub_reg_imm8(uint8_t reg, int8_t imm8, int fd, off_t *sz);
/* ) */
bool bfc_add_reg_imm16(uint8_t reg, int16_t imm16, int fd, off_t *sz);
/* ( */
bool bfc_sub_reg_imm16(uint8_t reg, int16_t imm16, int fd, off_t *sz);
/* $ */
bool bfc_add_reg_imm32(uint8_t reg, int32_t imm32, int fd, off_t *sz);
/* ^ */
bool bfc_sub_reg_imm32(uint8_t reg, int32_t imm32, int fd, off_t *sz);
/* n */
bool bfc_add_reg_imm64(uint8_t reg, int64_t imm64, int fd, off_t *sz);
/* n */
bool bfc_sub_reg_imm64(uint8_t reg, int64_t imm64, int fd, off_t *sz);
/* # */
bool bfc_add_mem(uint8_t reg, int8_t imm8, int fd, off_t *sz);
/* = */
bool bfc_sub_mem(uint8_t reg, int8_t imm8, int fd, off_t *sz);
/* @ */
bool bfc_zero_mem(uint8_t reg, int fd, off_t *sz);
#endif /* eam_instruction_encoders_h */
