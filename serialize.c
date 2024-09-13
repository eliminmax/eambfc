/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This defines functions to convert 64-bit ELF structs into LSB char arrays. */

/* C99 */
#include <stddef.h> /* size_t */
/* internal */
#include "compat/elf.h" /* Elf64_Ehdr, Elf64_Phdr */
#include "types.h" /* uint*_t, int*_t */

/* serialize a 16-bit value pointed to by v16 into 2 bytes in dest, in LSB order
 * return value is the number of bytes written. */
size_t serialize16(uint16_t u16, void *dest) {
    size_t size = 0;
    char *p = dest;
    uint8_t byte_val = u16;
    p[size++] = byte_val;
    byte_val = (u16 >> 8);
    p[size++] = byte_val;
    return size;
}

size_t serialize32(uint32_t u32, void *dest) {
    size_t size = serialize16(u32, dest);
    size += serialize16(u32 >> 16, (char *)dest + size);
    return size;
}

size_t serialize64(uint64_t u64, char *dest) {
    size_t size = serialize32(u64, dest);
    size += serialize32(u64 >> 32, (char *)dest + size);
    return size;
}

size_t serialize_ehdr64(Elf64_Ehdr* ehdr, void* dest) {
    size_t i;
    char *p = dest;
    /* first 16 bytes are easy - it's a series of literal byte values */
    for (i = 0; i < EI_NIDENT; i++) {
        p[i] = ehdr->e_ident[i];
    }
    i += serialize16(ehdr->e_type,      p + i);
    i += serialize16(ehdr->e_machine,   p + i);
    i += serialize32(ehdr->e_version,   p + i);
    i += serialize64(ehdr->e_entry,     p + i);
    i += serialize64(ehdr->e_phoff,     p + i);
    i += serialize64(ehdr->e_shoff,     p + i);
    i += serialize32(ehdr->e_flags,     p + i);
    i += serialize16(ehdr->e_ehsize,    p + i);
    i += serialize16(ehdr->e_phentsize, p + i);
    i += serialize16(ehdr->e_phnum,     p + i);
    i += serialize16(ehdr->e_shentsize, p + i);
    i += serialize16(ehdr->e_shnum,     p + i);
    i += serialize16(ehdr->e_shstrndx,  p + i);
    return i;
}

size_t serialize_phdr64(Elf64_Phdr* phdr, void* dest) {
    size_t i = 0;
    char *p = dest;
    i += serialize32(phdr->p_type,   p + i);
    i += serialize32(phdr->p_flags,  p + i);
    i += serialize64(phdr->p_offset, p + i);
    i += serialize64(phdr->p_vaddr,  p + i);
    i += serialize64(phdr->p_paddr,  p + i);
    i += serialize64(phdr->p_filesz, p + i);
    i += serialize64(phdr->p_memsz,  p + i);
    i += serialize64(phdr->p_align,  p + i);
    return i;
}
