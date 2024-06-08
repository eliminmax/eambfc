/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This defines functions to convert 64-bit ELF structs into LSB char arrays. */

/* C99 */
#include <stddef.h>
#include <stdint.h>
/* internal */
#include "compat/elf.h"

/* serialize a 16-bit value pointed to by v16 into 2 bytes in dest, in LSB order
 * return value is the byte after the 2 bytes are inserted */
size_t serialize16(uint16_t u16, char *dest) {
    size_t size = 0;
    uint8_t byte_val = (uint8_t)u16;
    *(dest + (size++)) = (char)byte_val;
    byte_val = (uint8_t)(u16 >> 8);
    *(dest + (size++)) = (char)byte_val;
    return size;
}

size_t serialize32(uint32_t u32, char *dest) {
    size_t size = serialize16((uint16_t)u32, dest);
    size += serialize16((uint16_t)(u32 >> 16), dest + size);
    return size;
}

size_t serialize64(uint64_t u64, char *dest) {
    size_t size = serialize32((uint32_t)u64, dest);
    size += serialize32((uint32_t)(u64 >> 32), dest + size);
    return size;
}

size_t serializeEhdr64(Elf64_Ehdr* ehdr, char* dest) {
    size_t i;
    /* first 16 bytes are easy - it's a series of literal byte values */
    for (i = 0; i < EI_NIDENT; i++) {
        *(dest + i) = ehdr->e_ident[i];
    }
    i += serialize16(ehdr->e_type,      dest + i);
    i += serialize16(ehdr->e_machine,   dest + i);
    i += serialize32(ehdr->e_version,   dest + i);
    i += serialize64(ehdr->e_entry,     dest + i);
    i += serialize64(ehdr->e_phoff,     dest + i);
    i += serialize64(ehdr->e_shoff,     dest + i);
    i += serialize32(ehdr->e_flags,     dest + i);
    i += serialize16(ehdr->e_ehsize,    dest + i);
    i += serialize16(ehdr->e_phentsize, dest + i);
    i += serialize16(ehdr->e_phnum,     dest + i);
    i += serialize16(ehdr->e_shentsize, dest + i);
    i += serialize16(ehdr->e_shnum,     dest + i);
    i += serialize16(ehdr->e_shstrndx,  dest + i);
    return i;
}

size_t serializePhdr64(Elf64_Phdr* phdr, char* dest) {
    size_t i = 0;
    i += serialize32(phdr->p_type,      dest + i);
    i += serialize32(phdr->p_flags,     dest + i);
    i += serialize64(phdr->p_offset,    dest + i);
    i += serialize64(phdr->p_vaddr,     dest + i);
    i += serialize64(phdr->p_paddr,     dest + i);
    i += serialize64(phdr->p_filesz,    dest + i);
    i += serialize64(phdr->p_memsz,     dest + i);
    i += serialize64(phdr->p_align,     dest + i);
    return i;
}
