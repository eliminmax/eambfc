/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This defines functions to convert 64-bit ELF structs into char arrays. */

/* C99 */
#include <stddef.h> /* size_t */
/* internal */
#include "compat/elf.h" /* Elf64_Ehdr, Elf64_Phdr */
#include "types.h" /* uint*_t, int*_t */

/* serialize a 16-bit value in u16 into 2 bytes in dest, in LSB order
 * return value is the number of bytes written. */
size_t serialize16le(uint16_t u16, void *dest) {
    size_t size = 0;
    char *p = dest;
    uint8_t byte_val = u16;
    p[size++] = byte_val;
    byte_val = (u16 >> 8);
    p[size++] = byte_val;
    return size;
}

/* serialize a 16-bit value in u16 into 2 bytes in dest, in MSB order
 * return value is the number of bytes written. */
size_t serialize16be(uint32_t u16, void *dest) {
    size_t size = 0;
    char *p = dest;
    uint8_t byte_val = (u16 >> 8);
    p[size++] = byte_val;
    byte_val = u16;
    p[size++] = byte_val;
    return size;
}

/* serialize a 32-bit value in u32 into 4 bytes in dest, in LSB order
 * return value is the number of bytes written. */
size_t serialize32le(uint32_t u32, void *dest) {
    size_t size = serialize16le(u32, dest);
    size += serialize16le(u32 >> 16, (char *)dest + size);
    return size;
}

/* serialize a 32-bit value in u32 into 4 bytes in dest, in MSB order
 * return value is the number of bytes written. */
size_t serialize32be(uint32_t u32, void *dest) {
    size_t size = serialize16be(u32 >> 16, dest);
    size += serialize16le(u32, (char *)dest + size);
    return size;
}

/* serialize a 64-bit value in u64 into 8 bytes in dest, in LSB order
 * return value is the number of bytes written. */
size_t serialize64le(uint64_t u64, char *dest) {
    size_t size = serialize32le(u64, dest);
    size += serialize32le(u64 >> 32, (char *)dest + size);
    return size;
}

/* serialize a 64-bit value in u64 into 8 bytes in dest, in MSB order
 * return value is the number of bytes written. */
size_t serialize64be(uint64_t u64, char *dest) {
    size_t size = serialize32be(u64 >> 32, dest);
    size += serialize32be(u64, (char *)dest + size);
    return size;
}

/* serialize a 64-bit Ehdr into a byte sequence, in LSB order */
size_t serialize_ehdr64_le(Elf64_Ehdr* ehdr, void* dest) {
    size_t i;
    char *p = dest;
    /* first 16 bytes are easy - it's a series of literal byte values */
    for (i = 0; i < EI_NIDENT; i++) {
        p[i] = ehdr->e_ident[i];
    }
    i += serialize16le(ehdr->e_type,      p + i);
    i += serialize16le(ehdr->e_machine,   p + i);
    i += serialize32le(ehdr->e_version,   p + i);
    i += serialize64le(ehdr->e_entry,     p + i);
    i += serialize64le(ehdr->e_phoff,     p + i);
    i += serialize64le(ehdr->e_shoff,     p + i);
    i += serialize32le(ehdr->e_flags,     p + i);
    i += serialize16le(ehdr->e_ehsize,    p + i);
    i += serialize16le(ehdr->e_phentsize, p + i);
    i += serialize16le(ehdr->e_phnum,     p + i);
    i += serialize16le(ehdr->e_shentsize, p + i);
    i += serialize16le(ehdr->e_shnum,     p + i);
    i += serialize16le(ehdr->e_shstrndx,  p + i);
    return i;
}

/* serialize a 64-bit Ehdr into a byte sequence, in MSB order */
size_t serialize_ehdr64_be(Elf64_Ehdr* ehdr, void* dest) {
    size_t i;
    char *p = dest;
    /* first 16 bytes are easy - it's a series of literal byte values */
    for (i = 0; i < EI_NIDENT; i++) {
        p[i] = ehdr->e_ident[i];
    }
    i += serialize16be(ehdr->e_type,      p + i);
    i += serialize16be(ehdr->e_machine,   p + i);
    i += serialize32be(ehdr->e_version,   p + i);
    i += serialize64be(ehdr->e_entry,     p + i);
    i += serialize64be(ehdr->e_phoff,     p + i);
    i += serialize64be(ehdr->e_shoff,     p + i);
    i += serialize32be(ehdr->e_flags,     p + i);
    i += serialize16be(ehdr->e_ehsize,    p + i);
    i += serialize16be(ehdr->e_phentsize, p + i);
    i += serialize16be(ehdr->e_phnum,     p + i);
    i += serialize16be(ehdr->e_shentsize, p + i);
    i += serialize16be(ehdr->e_shnum,     p + i);
    i += serialize16be(ehdr->e_shstrndx,  p + i);
    return i;
}

/* serialize a 64-bit Phdr into a byte sequence, in LSB order */
size_t serialize_phdr64_le(Elf64_Phdr* phdr, void* dest) {
    size_t i = 0;
    char *p = dest;
    i += serialize32le(phdr->p_type,   p + i);
    i += serialize32le(phdr->p_flags,  p + i);
    i += serialize64le(phdr->p_offset, p + i);
    i += serialize64le(phdr->p_vaddr,  p + i);
    i += serialize64le(phdr->p_paddr,  p + i);
    i += serialize64le(phdr->p_filesz, p + i);
    i += serialize64le(phdr->p_memsz,  p + i);
    i += serialize64le(phdr->p_align,  p + i);
    return i;
}

/* serialize a 64-bit Phdr into a byte sequence, in MSB order */
size_t serialize_phdr64_be(Elf64_Phdr* phdr, void* dest) {
    size_t i = 0;
    char *p = dest;
    i += serialize32be(phdr->p_type,   p + i);
    i += serialize32be(phdr->p_flags,  p + i);
    i += serialize64be(phdr->p_offset, p + i);
    i += serialize64be(phdr->p_vaddr,  p + i);
    i += serialize64be(phdr->p_paddr,  p + i);
    i += serialize64be(phdr->p_filesz, p + i);
    i += serialize64be(phdr->p_memsz,  p + i);
    i += serialize64be(phdr->p_align,  p + i);
    return i;
}
