/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This declares functions to convert sized integers and ELF structs to byte
 * sequences, in either LSB or MSB order */

#ifndef BFC_SERIALIZE_H
#define BFC_SERIALIZE_H 1
/* internal */
#include "attributes.h"
#include "types.h"

/* given an unsigned integer of a given size and destination pointer, these
 * write the bytes of the value to that pointer in LSB order.
 * This requires CHAR_BIT to be 8, which is required by POSIX anyway. */

/* Write the bytes of `v16` to `dest` in LSB order, returning the number of
 * written bytes. */
nonnull_args inline size_t serialize16le(u16 v16, void *dest) {
    ((u8 *)dest)[0] = v16;
    ((u8 *)dest)[1] = (v16 >> 8);
    return 2;
}

/* Write the bytes of `v32` to `dest` in LSB order, returning the number of
 * written bytes. */
nonnull_args inline size_t serialize32le(u32 v32, void *dest) {
    ((u8 *)dest)[0] = v32;
    ((u8 *)dest)[1] = (v32 >> 8);
    ((u8 *)dest)[2] = (v32 >> 16);
    ((u8 *)dest)[3] = (v32 >> 24);
    return 4;
}

/* Write the bytes of `v64` to `dest` in LSB order, returning the number of
 * written bytes. */
nonnull_args inline size_t serialize64le(u64 v64, void *dest) {
    ((u8 *)dest)[0] = v64;
    ((u8 *)dest)[1] = (v64 >> 8);
    ((u8 *)dest)[2] = (v64 >> 16);
    ((u8 *)dest)[3] = (v64 >> 24);
    ((u8 *)dest)[4] = (v64 >> 32);
    ((u8 *)dest)[5] = (v64 >> 40);
    ((u8 *)dest)[6] = (v64 >> 48);
    ((u8 *)dest)[7] = (v64 >> 56);
    return 8;
}

typedef struct ehdr_info {
    char e_ident[16];
    u64 e_entry;
    u32 e_flags;
    u16 e_machine;
    u16 e_phnum;
} ehdr_info;

typedef struct phdr_info {
    /* flags for the segment - of interest are the segment permissions, which
     * are similar to UNIX R+W+X file permissions - 04 is read, 02 is write,
     * and 01 is execute */
    u32 p_flags;
    u64 p_vaddr; /* virtual address of the segment */
    u64 p_filesz; /* size within the file */
    u64 p_memsz; /* size within memory */
    u64 p_align; /* alignment of segment */
} phdr_info;

/* serialize an elf header from the info in `ehdr`, and the info common to all
 * to all ehdrs `eambfc` would need to generate.
 *
 * Multibyte values are in LSB order*/
nonnull_args size_t
serialize_ehdr64_le(const ehdr_info *restrict ehdr, void *restrict dest);
/* write a program header table entry from the info in `phdr`, and the info
 * common to all phdr table entries generated by eambfc .
 *
 * Multibyte values are in LSB order*/
nonnull_args size_t
serialize_phdr64_le(const phdr_info *restrict phdr, void *restrict dest);

/* The same as the above, except in MSB order. */

/* Write the bytes of `v16` to `dest` in MSB order, returning the number of
 * written bytes. */
nonnull_args inline size_t serialize16be(u16 v16, void *dest) {
    ((u8 *)dest)[0] = (v16 >> 8);
    ((u8 *)dest)[1] = v16;
    return 2;
}

/* Write the bytes of `v32` to `dest` in MSB order, returning the number of
 * written bytes. */
nonnull_args inline size_t serialize32be(u32 v32, void *dest) {
    ((u8 *)dest)[0] = (v32 >> 24);
    ((u8 *)dest)[1] = (v32 >> 16);
    ((u8 *)dest)[2] = (v32 >> 8);
    ((u8 *)dest)[3] = v32;
    return 4;
}

/* Write the bytes of `v64` to `dest` in MSB order, returning the number of
 * written bytes. */
nonnull_args inline size_t serialize64be(u64 v64, void *dest) {
    ((u8 *)dest)[0] = (v64 >> 56);
    ((u8 *)dest)[1] = (v64 >> 48);
    ((u8 *)dest)[2] = (v64 >> 40);
    ((u8 *)dest)[3] = (v64 >> 32);
    ((u8 *)dest)[4] = (v64 >> 24);
    ((u8 *)dest)[5] = (v64 >> 16);
    ((u8 *)dest)[6] = (v64 >> 8);
    ((u8 *)dest)[7] = v64;
    return 8;
}

/* serialize an elf header from the info in `ehdr`, and the info common to all
 * to all ehdrs `eambfc` would need to generate.
 *
 * Multibyte values are in MSB order*/
nonnull_args size_t
serialize_ehdr64_be(const ehdr_info *restrict ehdr, void *restrict dest);
/* write a program header table entry from the info in `phdr`, and the info
 * common to all phdr table entries generated by eambfc .
 *
 * Multibyte values are in MSB order*/
nonnull_args size_t
serialize_phdr64_be(const phdr_info *restrict phdr, void *restrict dest);

#endif /* BFC_SERIALIZE_H */
