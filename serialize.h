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
#include "compat/elf.h"
#include "types.h"

/* by defining BFC_SERIALIZE_C in serialize.c, needing duplicate definitions of
 * these functions is avoided */
#ifdef BFC_SERIALIZE_C
#define inline_impl extern
#else
#define inline_impl inline
#endif /* BFC_SERIALIZE_H */

/* given an unsigned integer of a given size and destination pointer, these
 * write the bytes of the value to that pointer in LSB order.
 * This requires CHAR_BIT to be 8, which is required by POSIX anyway. */
nonnull_args inline_impl size_t serialize16le(u16 v16, void *dest) {
    ((char *)dest)[0] = v16;
    ((char *)dest)[1] = (v16 >> 8);
    return 2;
}

nonnull_args inline_impl size_t serialize32le(u32 v32, void *dest) {
    ((char *)dest)[0] = v32;
    ((char *)dest)[1] = (v32 >> 8);
    ((char *)dest)[2] = (v32 >> 16);
    ((char *)dest)[3] = (v32 >> 24);
    return 4;
}

nonnull_args inline_impl size_t serialize64le(u64 v64, void *dest) {
    ((char *)dest)[0] = v64;
    ((char *)dest)[1] = (v64 >> 8);
    ((char *)dest)[2] = (v64 >> 16);
    ((char *)dest)[3] = (v64 >> 24);
    ((char *)dest)[4] = (v64 >> 32);
    ((char *)dest)[5] = (v64 >> 40);
    ((char *)dest)[6] = (v64 >> 48);
    ((char *)dest)[7] = (v64 >> 56);
    return 8;
}

/* given a pointers to a struct of a given type and a destination pointer, these
 * write the fields of the struct in LSB order to dest (without padding). */
nonnull_args size_t
serialize_ehdr64_le(const Elf64_Ehdr *ehdr, void *dest); /* Elf64_Ehdr */
nonnull_args size_t
serialize_phdr64_le(const Elf64_Phdr *phdr, void *dest); /* Elf64_Phdr */

/* The same as the above, except in MSB order. */
nonnull_args inline_impl size_t serialize16be(u16 v16, void *dest) {
    ((char *)dest)[0] = (v16 >> 8);
    ((char *)dest)[1] = v16;
    return 2;
}

nonnull_args inline_impl size_t serialize32be(u32 v32, void *dest) {
    ((char *)dest)[0] = (v32 >> 24);
    ((char *)dest)[1] = (v32 >> 16);
    ((char *)dest)[2] = (v32 >> 8);
    ((char *)dest)[3] = v32;
    return 4;
}

nonnull_args inline_impl size_t serialize64be(u64 v64, void *dest) {
    ((char *)dest)[0] = (v64 >> 56);
    ((char *)dest)[1] = (v64 >> 48);
    ((char *)dest)[2] = (v64 >> 40);
    ((char *)dest)[3] = (v64 >> 32);
    ((char *)dest)[4] = (v64 >> 24);
    ((char *)dest)[5] = (v64 >> 16);
    ((char *)dest)[6] = (v64 >> 8);
    ((char *)dest)[7] = v64;
    return 8;
}

nonnull_args size_t
serialize_ehdr64_be(const Elf64_Ehdr *ehdr, void *dest); /* Elf64_Ehdr */
nonnull_args size_t
serialize_phdr64_be(const Elf64_Phdr *phdr, void *dest); /* Elf64_Phdr */
#endif /* BFC_SERIALIZE_H */
