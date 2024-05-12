/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h> /* C99 */
#include <arpa/inet.h> /* POSIX.1-2001 */
#include "elf.h"

/* internal macros for serializing 16, 32, and 64-bit values with little-endian
 * byte ordering to position `index` within a char array `dest`, incrementing
 * index. */
#define _serialize16(v16) \
    dest[index++] = v16; \
    dest[index++] = v16 >> 8

#define _serialize32(v32) \
    _serialize16(v32); \
    _serialize16(v32 >> 16)

#define _serialize64(v64) \
    _serialize32(v64); \
    _serialize32(v64 >> 32)


void serializeEhdr64(Elf64_Ehdr* ehdr, char* dest) {
    uint8_t index;
    /* first 16 bytes are easy - it's a series of literal byte values */
    for (index = 0; index < EI_NIDENT; index++) {
        dest[index] = ehdr->e_ident[index];
    }
    _serialize16(ehdr->e_type);
    _serialize16(ehdr->e_machine);
    _serialize32(ehdr->e_version);
    _serialize64(ehdr->e_entry);
    _serialize64(ehdr->e_phoff);
    _serialize64(ehdr->e_shoff);
    _serialize32(ehdr->e_flags);
    _serialize16(ehdr->e_ehsize);
    _serialize16(ehdr->e_phentsize);
    _serialize16(ehdr->e_phnum);
    _serialize16(ehdr->e_shentsize);
    _serialize16(ehdr->e_shnum);
    _serialize16(ehdr->e_shstrndx);
}

void serializePhdr64(Elf64_Phdr* phdr, char* dest) {
    uint8_t index = 0;
    _serialize32(phdr->p_type);
    _serialize32(phdr->p_flags);
    _serialize64(phdr->p_offset);
    _serialize64(phdr->p_vaddr);
    _serialize64(phdr->p_paddr);
    _serialize64(phdr->p_filesz);
    _serialize64(phdr->p_memsz);
    _serialize64(phdr->p_align);
}
