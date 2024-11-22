/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file declares the interface to the serialize functions in serialize.c */

#ifndef EAMBFC_SERIALIZE_H
#define EAMBFC_SERIALIZE_H 1
/* internal */
#include "compat/elf.h" /* Elf64_Ehdr, Elf64_Phdr */
#include "types.h" /* [iu]{8,16,32,64} */

/* given an unsigned integer of a given size and a char array, these write the
 * value to the char array, in LSB order, returning the number of bytes written.
 * This requires CHAR_BIT to be 8, which is required by POSIX anyway. */
size_t serialize16le(u16 v16, void *dest);
size_t serialize32le(u32 v32, void *dest);
size_t serialize64le(u64 v64, void *dest);

/* given a pointers to a struct of a given type and a char array, these write
 * the fields of the struct in LSB order to the char array without padding . */
size_t serialize_ehdr64_le(Elf64_Ehdr *ehdr, void *dest); /* Elf64_Ehdr */
size_t serialize_phdr64_le(const Elf64_Phdr *phdr, void *dest); /* Elf64_Phdr */

/* The same as the above, except in MSB order. */
size_t serialize16be(u16 v16, void *dest);
size_t serialize32be(u32 v32, void *dest);
size_t serialize64be(u64 v64, void *dest);
size_t serialize_ehdr64_be(Elf64_Ehdr *ehdr, void *dest); /* Elf64_Ehdr */
size_t serialize_phdr64_be(const Elf64_Phdr *phdr, void *dest); /* Elf64_Phdr */
#endif /* EAMBFC_SERIALIZE_H */
