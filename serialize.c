/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This defines functions to convert 64-bit ELF structs into LSB char arrays. */

/* C99 */
#include <stdint.h>
/* internal */
#include "compat/elf.h"

/* internal macros for serializing 16, 32, and 64-bit values with little-endian
 * byte ordering to position `index` within a char array `dest`, incrementing
 * index. */
#define serialize16(v16) \
    dest[index++] = v16; \
    dest[index++] = v16 >> 8

#define serialize32(v32) \
    serialize16(v32); \
    serialize16(v32 >> 16)

#define serialize64(v64) \
    serialize32(v64); \
    serialize32(v64 >> 32)


void serializeEhdr64(Elf64_Ehdr* ehdr, char* dest) {
    uint8_t index;
    /* first 16 bytes are easy - it's a series of literal byte values */
    for (index = 0; index < EI_NIDENT; index++) {
        dest[index] = ehdr->e_ident[index];
    }
    serialize16(ehdr->e_type);
    serialize16(ehdr->e_machine);
    serialize32(ehdr->e_version);
    serialize64(ehdr->e_entry);
    serialize64(ehdr->e_phoff);
    serialize64(ehdr->e_shoff);
    serialize32(ehdr->e_flags);
    serialize16(ehdr->e_ehsize);
    serialize16(ehdr->e_phentsize);
    serialize16(ehdr->e_phnum);
    serialize16(ehdr->e_shentsize);
    serialize16(ehdr->e_shnum);
    serialize16(ehdr->e_shstrndx);
}

void serializePhdr64(Elf64_Phdr* phdr, char* dest) {
    uint8_t index = 0;
    serialize32(phdr->p_type);
    serialize32(phdr->p_flags);
    serialize64(phdr->p_offset);
    serialize64(phdr->p_vaddr);
    serialize64(phdr->p_paddr);
    serialize64(phdr->p_filesz);
    serialize64(phdr->p_memsz);
    serialize64(phdr->p_align);
}
