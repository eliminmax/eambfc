/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains functions that encode x86_64 machine instructions and
 * write them to open file descriptors. Each returns a boolean value indicating
 * whether or not the write was successful. */
#ifndef INSTRUCTION_ENCODERS_H
#define INSTRUCTION_ENCODERS_H 1
/* C99 */
#include <stdbool.h>
#include <stdint.h>

/* used for making system calls, setup, and other miscellaneous things */
bool eamAsmSetReg(unsigned char reg, int32_t imm, int fd, off_t *sz);
bool eamAsmRegCopy(unsigned char dst, unsigned char src, int fd, off_t *sz);
bool eamAsmSyscall(int fd, off_t *sz);

/* ] */
bool eamAsmJumpNotZero(unsigned char reg, int32_t offset, int fd, off_t *sz);
/* [ */
bool eamAsmJumpZero(unsigned char reg, int32_t offset, int fd, off_t *sz);
/* > */
bool eamAsmIncReg(unsigned char reg, int fd, off_t *sz);
/* < */
bool eamAsmDecReg(unsigned char reg, int fd, off_t *sz);
/* + */
bool eamAsmIncByte(unsigned char reg, int fd, off_t *sz);
/* - */
bool eamAsmDecByte(unsigned char reg, int fd, off_t *sz);

/* TODO: UNIMPLEMENTED machine code for extra instructions from eambfc-ir */
/* } */
bool eamAsmAddRegByte(unsigned char reg, int8_t imm8, int fd, off_t *sz);
/* { */
bool eamAsmSubRegByte(unsigned char reg, int8_t imm8, int fd, off_t *sz);
/* ) */
bool eamAsmAddRegWord(unsigned char reg, int16_t imm16, int fd, off_t *sz);
/* ( */
bool eamAsmSubRegWord(unsigned char reg, int16_t imm16, int fd, off_t *sz);
/* $ */
bool eamAsmAddRegDoubWord(unsigned char reg, int32_t imm32, int fd, off_t *sz);
/* ^ */
bool eamAsmSubRegDoubWord(unsigned char reg, int32_t imm32, int fd, off_t *sz);
/* n */
bool eamAsmAddRegQuadWord(unsigned char reg, int64_t imm64, int fd, off_t *sz);
/* N */
bool eamAsmSubRegQuadWord(unsigned char reg, int64_t imm64, int fd, off_t *sz);
/* # */
bool eamAsmAddMem(unsigned char rec, int8_t imm8, int fd, off_t *sz);
/* = */
bool eamAsmSubMem(unsigned char rec, int8_t imm8, int fd, off_t *sz);
/* @ */
bool eamAsmSetMemZero(unsigned char reg, int fd, off_t *sz);
#endif /* INSTRUCTION_ENCODERS_H */
