/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This defines functions to convert sized integers and ELF structs to byte
 * sequences, in either LSB or MSB order
 *
 * Given that the MSB and LSB versions are the same except for the byte order,
 * some nasty preprocessor magic is used to implement the ELF struct functions.
 * I'm not^H^H^H^H sorry. */

/* C99 */
#include <stddef.h>
#include <string.h>
/* internal */
/* need to define before including serialize.h, so that inline functions */
#define BFC_SERIALIZE_C
#include "serialize.h"

/* the function body for both serialize_ehdr64_le and serialize_ehdr64_be
 * Did not use token pasting as treesitter does not support parsing it, which
 * breaks syntax highlighting in Neovim, and I find it messy anyway.
 * (https://github.com/tree-sitter/tree-sitter-c/issues/98) */
#define IMPL_EHDR64(serialize16, serialize32, serialize64) \
    /* first 16 bytes are easy - it's a series of literal byte values */ \
    memcpy(dest, ehdr->e_ident, EI_NIDENT); \
    char *p = &((char *)dest)[EI_NIDENT]; \
    size_t i = 0; \
    i += serialize16(ehdr->e_type, p + i); \
    i += serialize16(ehdr->e_machine, p + i); \
    i += serialize32(ehdr->e_version, p + i); \
    i += serialize64(ehdr->e_entry, p + i); \
    i += serialize64(ehdr->e_phoff, p + i); \
    i += serialize64(ehdr->e_shoff, p + i); \
    i += serialize32(ehdr->e_flags, p + i); \
    i += serialize16(ehdr->e_ehsize, p + i); \
    i += serialize16(ehdr->e_phentsize, p + i); \
    i += serialize16(ehdr->e_phnum, p + i); \
    i += serialize16(ehdr->e_shentsize, p + i); \
    i += serialize16(ehdr->e_shnum, p + i); \
    i += serialize16(ehdr->e_shstrndx, p + i); \
    return i

/* serialize a 64-bit Ehdr into a byte sequence, in LSB order */
nonnull_args size_t serialize_ehdr64_le(const Elf64_Ehdr *ehdr, void *dest) {
    IMPL_EHDR64(serialize16le, serialize32le, serialize64le);
}

/* serialize a 64-bit Ehdr into a byte sequence, in MSB order */
nonnull_args size_t serialize_ehdr64_be(const Elf64_Ehdr *ehdr, void *dest) {
    IMPL_EHDR64(serialize16be, serialize32be, serialize64be);
}

#define IMPL_PHDR64(serialize32, serialize64) \
    size_t i = 0; \
    char *p = dest; \
    i += serialize32(phdr->p_type, p + i); \
    i += serialize32(phdr->p_flags, p + i); \
    i += serialize64(phdr->p_offset, p + i); \
    i += serialize64(phdr->p_vaddr, p + i); \
    i += serialize64(phdr->p_paddr, p + i); \
    i += serialize64(phdr->p_filesz, p + i); \
    i += serialize64(phdr->p_memsz, p + i); \
    i += serialize64(phdr->p_align, p + i); \
    return i

/* serialize a 64-bit Phdr into a byte sequence, in LSB order */
nonnull_args size_t serialize_phdr64_le(const Elf64_Phdr *phdr, void *dest) {
    IMPL_PHDR64(serialize32le, serialize64le);
}

/* serialize a 64-bit Phdr into a byte sequence, in MSB order */
nonnull_args size_t serialize_phdr64_be(const Elf64_Phdr *phdr, void *dest) {
    IMPL_PHDR64(serialize32be, serialize64be);
}

#ifdef BFC_TEST
/* internal */
#include "unit_test.h"

static void test_serialize_nums(void) {
    char dest16le[3] = {0};
    char dest16be[3] = {0};
    char dest32le[5] = {0};
    char dest32be[5] = {0};
    char dest64le[9] = {0};
    char dest64be[9] = {0};
    const uchar expect16le[3] = {0xef, 0xbe};
    const uchar expect16be[3] = {0xbe, 0xef};
    const uchar expect32le[5] = {0xef, 0xbe, 0xad, 0xde};
    const uchar expect32be[5] = {0xde, 0xad, 0xbe, 0xef};
    const uchar expect64le[9] = {0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x1};
    const uchar expect64be[9] = {0x1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    serialize16le(0xbeef, dest16le);
    serialize16be(0xbeef, dest16be);
    serialize32le(0xdeadbeef, dest32le);
    serialize32be(0xdeadbeef, dest32be);
    serialize64le(0x123456789abcdef, dest64le);
    serialize64be(0x123456789abcdef, dest64be);
    CU_ASSERT_NSTRING_EQUAL(dest16le, expect16le, 3);
    CU_ASSERT_NSTRING_EQUAL(dest16be, expect16be, 3);
    CU_ASSERT_NSTRING_EQUAL(dest32le, expect32le, 5);
    CU_ASSERT_NSTRING_EQUAL(dest32be, expect32be, 5);
    CU_ASSERT_NSTRING_EQUAL(dest64le, expect64le, 9);
    CU_ASSERT_NSTRING_EQUAL(dest64be, expect64be, 9);
}

CU_pSuite register_serialize_tests(void) {
    CU_pSuite suite = CU_add_suite("serialize", NULL, NULL);
    if (suite == NULL) return NULL;
    ERRORCHECKED(CU_ADD_TEST(suite, test_serialize_nums));
    return suite;
}

#endif /* BFC_TEST */
