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

bool eamAsmDecByte(unsigned char reg, int fd);
bool eamAsmSetReg(unsigned char reg, int32_t imm, int fd);
bool eamAsmRegCopy(unsigned char dst, unsigned char src, int fd);
bool eamAsmSyscall(int fd);

/* ] */
bool eamAsmJumpNotZero(unsigned char reg, int32_t offset, int fd);
/* [ */
bool eamAsmJumpZero(unsigned char reg, int32_t offset, int fd);
/* > */
bool eamAsmIncReg(unsigned char reg, int fd);
/* < */
bool eamAsmDecReg(unsigned char reg, int fd);
/* + */
bool eamAsmIncByte(unsigned char reg, int fd);
/* - */
bool eamAsmDecByte(unsigned char reg, int fd);

/* TODO: UNIMPLEMENTED machine code for extra instructions from eambfc-ir */
/* } */
bool eamAsmAddRegByte(unsigned char reg, int8_t imm8, int fd);
/* { */
bool eamAsmSubRegByte(unsigned char reg, int8_t imm8, int fd);
/* ) */
bool eamAsmAddRegWord(unsigned char reg, int16_t imm16, int fd);
/* ( */
bool eamAsmSubRegWord(unsigned char reg, int16_t imm16, int fd);
/* $ */
bool eamAsmAddRegDoubleWord(unsigned char reg, int32_t imm32, int fd);
/* ^ */
bool eamAsmSubRegDoubleWord(unsigned char reg, int32_t imm32, int fd);
/* n */
bool eamAsmAddRegQuadWord(unsigned char reg, int64_t imm64, int fd);
/* N */
bool eamAsmSubRegQuadWord(unsigned char reg, int64_t imm64, int fd);
/* # */
bool eamAsmAddMem(unsigned char rec, int8_t imm8, int fd);
/* = */
bool eamAsmSubMem(unsigned char rec, int8_t imm8, int fd);
/* @ */
bool eamAsmSetMemZero(unsigned char reg, int fd);
#endif /* INSTRUCTION_ENCODERS_H */
