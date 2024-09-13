/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file declares the interface to the serialize functions in serialize.c */


#ifndef EAMBFC_SERIALIZE_H
#define EAMBFC_SERIALIZE_H 1
/* internal */
#include "compat/elf.h" /* Elf64_Ehdr, Elf64_Phdr */
#include "types.h" /* uint*_t */

/* given an unsigned integer of a given size and a char array, these write the
 * value to the char array, in LSB order, returning the number of bytes written.
 * This requires CHAR_BIT to be 8, which is required by POSIX anyway. */
size_t serialize16(uint16_t u16, void *dest);
size_t serialize32(uint32_t u32, void *dest);
size_t serialize64(uint64_t u64, void *dest);

/* given a pointers to a struct of a given type and a char array, these write
 * the fields of the struct, in little-endian order, to the char array, with no
 * padding bytes. */
size_t serialize_ehdr64(Elf64_Ehdr* ehdr, void* dest); /* Elf64_Ehdr */
size_t serialize_phdr64(Elf64_Phdr* phdr, void* dest); /* Elf64_Phdr */
#endif /* EAMBFC_SERIALIZE_H */
