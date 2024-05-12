/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */


#ifndef EAMBFC_SERIALIZE
#define EAMBFC_SERIALIZE
#include <elf.h>

/* given a pointers to a struct of a given type and a char array, these write
 * the fields of the struct, in little-endian order, to the byte array, with no
 * padding bytes. */
void serializeEhdr64(Elf64_Ehdr* ehdr, char* dest); /* Elf64_Ehdr */
void serializePhdr64(Elf64_Phdr* phdr, char* dest); /* Elf64_Phdr */
#endif /* EAMBFC_SERIALIZE */
