/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file declares the interface to the serialize functions in serialize.c */


#ifndef EAMBFC_SERIALIZE_H
#define EAMBFC_SERIALIZE_H 1
/* C99 */
#include <stdint.h>
/* internal */
#include "compat/elf.h"

/* given an unsigned integer of a given size and a char array, these write the
 * value to the char array, in LSB order, returning the number of bytes written.
 * This requires CHAR_BIT to be 8, which is required by POSIX anyway. */
size_t serialize16(uint16_t u16, char *dest);
size_t serialize32(uint32_t u32, char *dest);
size_t serialize64(uint64_t u64, char *dest);
/* given a pointers to a struct of a given type and a char array, these write
 * the fields of the struct, in little-endian order, to the char array, with no
 * padding bytes. */
size_t serializeEhdr64(Elf64_Ehdr* ehdr, char* dest); /* Elf64_Ehdr */
size_t serializePhdr64(Elf64_Phdr* phdr, char* dest); /* Elf64_Phdr */
#endif /* EAMBFC_SERIALIZE_H */
