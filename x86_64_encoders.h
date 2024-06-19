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
/* C99 */
#include <stdbool.h>
#include <stdint.h>
/* POSIX */
#include <unistd.h>

/* used for making system calls, setup, and other miscellaneous things */
bool eamAsmSetReg(uint8_t reg, int32_t imm, int fd, off_t *sz);
bool eamAsmRegCopy(uint8_t dst, uint8_t src, int fd, off_t *sz);
bool eamAsmSyscall(int fd, off_t *sz);

/* ] */
bool eamAsmJumpNotZero(uint8_t reg, int32_t offset, int fd, off_t *sz);
/* [ */
bool eamAsmJumpZero(uint8_t reg, int32_t offset, int fd, off_t *sz);
/* > */
bool eamAsmIncReg(uint8_t reg, int fd, off_t *sz);
/* < */
bool eamAsmDecReg(uint8_t reg, int fd, off_t *sz);
/* + */
bool eamAsmIncByte(uint8_t reg, int fd, off_t *sz);
/* - */
bool eamAsmDecByte(uint8_t reg, int fd, off_t *sz);

/* machine code for extra instructions from eambfc-ir */
/* } */
bool eamAsmAddRegByte(uint8_t reg, int8_t imm8, int fd, off_t *sz);
/* { */
bool eamAsmSubRegByte(uint8_t reg, int8_t imm8, int fd, off_t *sz);
/* ) */
bool eamAsmAddRegWord(uint8_t reg, int16_t imm16, int fd, off_t *sz);
/* ( */
bool eamAsmSubRegWord(uint8_t reg, int16_t imm16, int fd, off_t *sz);
/* $ */
bool eamAsmAddRegDoubWord(uint8_t reg, int32_t imm32, int fd, off_t *sz);
/* ^ */
bool eamAsmSubRegDoubWord(uint8_t reg, int32_t imm32, int fd, off_t *sz);
/* n */
bool eamAsmAddRegQuadWord(uint8_t reg, int64_t imm64, int fd, off_t *sz);
/* N */
bool eamAsmSubRegQuadWord(uint8_t reg, int64_t imm64, int fd, off_t *sz);
/* # */
bool eamAsmAddMem(uint8_t reg, int8_t imm8, int fd, off_t *sz);
/* = */
bool eamAsmSubMem(uint8_t reg, int8_t imm8, int fd, off_t *sz);
/* @ */
bool eamAsmSetMemZero(uint8_t reg, int fd, off_t *sz);
#endif /* EAM_INSTRUCTION_ENCODERS_H */
