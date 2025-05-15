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
#include <string.h>
/* internal */
#include <types.h>

#include "arch_inter.h"
#include "serialize.h"

extern inline size_t serialize16le(u16 v16, void *dest);
extern inline size_t serialize32le(u32 v32, void *dest);
extern inline size_t serialize64le(u64 v64, void *dest);
extern inline size_t serialize16be(u16 v16, void *dest);
extern inline size_t serialize32be(u32 v32, void *dest);
extern inline size_t serialize64be(u64 v64, void *dest);

/* write an elf header using the provided functions and values.
 *
 * * s16 is the function to serialize 16-bit fields
 * * s32 is the function to serialize fields which are 32-bit regardless of
 *      address size
 * * s_addr is the function to serialize fields which are the same size as the
 *      target's address size
 * * ehdz is the size of the ELF header itself
 * * phentsz is the size of program header table entries */
#define WRITE_EHDR(s16, s32, s_addr, ehsz, phentsz) \
    /* first 16 bytes are easy - it's a series of literal byte values */ \
    memcpy(dest, ehdr->e_ident, 16); \
    char *p = ((char *)dest) + 16; \
    p += s16(2, p); /* 2 is ET_EXEC */ \
    p += s16(ehdr->e_machine, p); \
    p += s32(1, p); /* 1 is EV_CURRENT (the only legal value) */ \
    p += s_addr(ehdr->e_entry, p); \
    p += s_addr(ehsz, p); /* phdr table offset is right after ehdr */ \
    p += s_addr(0, p); /* w/o shdr table, shoff offset is 0 */ \
    p += s32(ehdr->e_flags, p); \
    p += s16(ehsz, p); /* size of a 64-bit Ehdr */ \
    p += s16(phentsz, p); /* size of a 64-bit Phdr table entry */ \
    p += s16(ehdr->e_phnum, p); \
    p += s16(0, p); /* w/o shdr table, shentsize is 0 */ \
    p += s16(0, p); /* w/o shdr table, shnum is 0 */ \
    p += s16(0, p); /* w/o shdr table, shstrndx is 0 */ \
    return ehsz

#define WRITE_EHDR64(bo) \
    WRITE_EHDR(serialize16##bo, serialize32##bo, serialize64##bo, 64, 56)
#define WRITE_EHDR32(bo) \
    WRITE_EHDR(serialize16##bo, serialize32##bo, serialize32##bo, 52, 32)

/* serialize a 64-bit Ehdr into a byte sequence, in LSB order */
nonnull_args size_t
serialize_ehdr_le(const ElfInfo *restrict ehdr, void *restrict dest) {
    if (ehdr->e_ident[4] == PTRSIZE_32) {
        WRITE_EHDR32(le);
    } else {
        WRITE_EHDR64(le);
    }
}

/* serialize a 64-bit Ehdr into a byte sequence, in MSB order */
nonnull_args size_t
serialize_ehdr_be(const ElfInfo *restrict ehdr, void *restrict dest) {
    if (ehdr->e_ident[4] == PTRSIZE_32) {
        WRITE_EHDR32(be);
    } else {
        WRITE_EHDR64(be);
    }
}

/* field order is different between 32-bit and 64-bit elf files, so can't use
 * the same underlying macro for both 32-bit and 64-bit variants. */

#define IMPL_PHDR32(serialize32) \
    char *p = dest; \
    p += serialize32(1, p); /* PT_LOAD, the only type needed in eambfc */ \
    /* file offset of the segment - 0 for both the tape segment, which doesn't \
     * draw from the file at all, and the code segment, which includes the \
     * whole file. */ \
    p += serialize32(0, p); \
    /* virtual memory address to load the segment into */ \
    /* virtual address of the segment */ \
    p += serialize32(phdr->virtaddr, p); \
    /* physical memory address is always 0 on Linux */ \
    p += serialize32(0, p); \
    /* size within the file */ \
    p += serialize32(phdr->file_backed ? phdr->size : 0, p); \
    p += serialize32(phdr->size, p); /* size within memory */ \
    p += serialize32(phdr->p_flags, p); /* flags for the segment */ \
    p += serialize32(phdr->p_align, p); /* alignment of segment */ \
    return 32

#define IMPL_PHDR64(serialize32, serialize64) \
    char *p = dest; \
    p += serialize32(1, p); /* PT_LOAD, the only type needed in eambfc */ \
    p += serialize32(phdr->p_flags, p); /* flags for the segment */ \
    /* file offset of the segment - 0 for both the tape segment, which doesn't \
     * draw from the file at all, and the code segment, which includes the \
     * whole file. */ \
    p += serialize64(0, p); \
    /* virtual memory address to load the segment into */ \
    /* virtual address of the segment */ \
    p += serialize64(phdr->virtaddr, p); \
    /* physical memory address is always 0 on Linux */ \
    p += serialize64(0, p); \
    /* size within the file */ \
    p += serialize64(phdr->file_backed ? phdr->size : 0, p); \
    p += serialize64(phdr->size, p); /* size within memory */ \
    p += serialize64(phdr->p_align, p); /* alignment of segment */ \
    return 56

/* serialize a 64-bit Phdr into a byte sequence, in LSB order */
nonnull_args size_t
serialize_phdr_le(const SegmentInfo *restrict phdr, void *restrict dest) {
    if (phdr->addr_size == PTRSIZE_32) {
        IMPL_PHDR32(serialize32le);
    } else {
        IMPL_PHDR64(serialize32le, serialize64le);
    }
}

/* serialize a 64-bit Phdr into a byte sequence, in MSB order */
nonnull_args size_t
serialize_phdr_be(const SegmentInfo *restrict phdr, void *restrict dest) {
    if (phdr->addr_size == PTRSIZE_32) {
        IMPL_PHDR32(serialize32be);
    } else {
        IMPL_PHDR64(serialize32be, serialize64be);
    }
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
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, test_serialize_nums);
    return suite;
}

#endif /* BFC_TEST */
