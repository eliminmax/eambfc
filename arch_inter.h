/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains typedefs of structs used to provide the interface to
 * architectures, and declares all supported backends at the end. */

#ifndef BFC_ARCH_INTER_H
#define BFC_ARCH_INTER_H 1
/* internal */
#include "config.h"
#include "types.h"

/* Once an interface is defined and implemented, it needs to be integrated into
 * the rest of the system. First, restore the removed `e_machine` value in
 * `"compat/elf.h"`, then find all the places that have the text
 * `"__BACKENDS__"`, and follow the instructions to hook it in.
 *
 * For all of these steps, It's best to copy the ARM64 backend and change the
 * ARM64 and AARCH64 identifiers out for your backend, for the sake of
 * consistency. */

/* registers that can be passed to the functions in `arch_funcs`, for the
 * documented purposes */

/* This struct contains pointers to the functions that are actually used to
 * implement the backend.
 *
 * All functions in here must return `true` on success, and `false` on failure.
 * On failure, they should also use functions declared in `err.h` to print
 * errorlsp messages before returning.
 *
 * Registers are passed to these functions as u8 values. They should be the
 * values used within machine code to identify the registers, but if needed,
 * they can be opaque identifiers that a function not included in this struct
 * can handle as needed. The registers passed to calls through these pointers
 * must be from the corresponding `arch_regsiters` */
typedef const struct arch_inter {
    /* the read system call number */
    i64 sc_read;
    /* the write system call number */
    i64 sc_write;
    /* the exit system call number */
    i64 sc_exit;

    /* Write instruction/s to dst_buf to store immediate imm in register reg. */
    void (*const set_reg)(u8 reg, i64 imm, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to copy value stored in register src into
     * register dst. */
    void (*const reg_copy)(u8 dst, u8 src, sized_buf *dst_buf);

    /* Write the system call instruction to dst_buf. */
    void (*const syscall)(sized_buf *dst_buf);

    /* write an instruction, then pad with no-op instructions. Must use the same
     * number of bytes as `jump_zero` */
    void (*const pad_loop_open)(sized_buf *dst_buf);

    /* Functions that correspond 1 to 1 with brainfuck instructions.
     * Note that the `.` and `,` instructions are implemented using more complex
     * combinations of the above functions, as they involve setting multiple
     * argument registers to either fixed values or the contents of the bf_ptr
     * register, and calling the syscall instruction. */

    /* Write instruction/s to dst_buf to jump offset bytes if the byte stored at
     * address in register reg is set to zero.
     *
     * Used to implement the `[` brainfuck instruction. */
    bool (*const jump_zero)(u8 reg, i64 offset, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to jump <offset> bytes if the byte stored
     * at address in register reg is not set to zero.
     *
     * Used to implement the `]` brainfuck instruction. */
    bool (*const jump_not_zero)(u8 reg, i64 offset, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to increment register reg by one.
     *
     * Used to implement the `>` brainfuck instruction. */
    void (*const inc_reg)(u8 reg, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to decrement register reg by one.
     *
     * Used to implement the `<` brainfuck instruction. */
    void (*const dec_reg)(u8 reg, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to increment byte stored at address in
     * register reg by one.
     *
     * Used to implement the `+` brainfuck instruction. */
    void (*const inc_byte)(u8 reg, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to decrement byte stored at address in
     * register reg by one.
     *
     * Used to implement the `-` brainfuck instruction. */
    void (*const dec_byte)(u8 reg, sized_buf *dst_buf);

    /* functions used for optimized instructions */

    /* Write instruction/s to dst_buf to add imm to register reg.
     *
     * Used to implement sequences of consecutive `>` brainfuck instructions. */
    void (*const add_reg)(u8 reg, u64 imm, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to subtract imm from register reg.
     *
     * Used to implement sequences of consecutive `<` brainfuck instructions. */
    void (*const sub_reg)(u8 reg, u64 imm, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to add imm8 to byte stored at address in
     * register reg.
     *
     * Used to implement sequences of consecutive `+` brainfuck instructions. */
    void (*const add_byte)(u8 reg, u8 imm8, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to subtract imm8 from byte stored at
     * address in register reg.
     *
     * Used to implement sequences of consecutive `-` brainfuck instructions. */
    void (*const sub_byte)(u8 reg, u8 imm8, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to set the value of byte stored at address
     * in register reg to 0.
     *
     * Used to implement the `[-]` and `[+]` brainfuck instruction sequences. */
    void (*const zero_byte)(u8 reg, sized_buf *dst_buf);

    /* CPU flags that should be set for executables for this architecture. */
    u32 flags;
    /* The 16-bit EM_* identifier for the architecture, from elf.h */
    u16 elf_arch;
    /* ELF EHDR value for endianness - either `ELFDATA2LSB` or `ELFDATA2MSB`,
     * depending on byte ordering of the backend. */
    unsigned char elf_data;

    /* register Linux uses for system call number */
    u8 reg_sc_num;
    /* registers Linux uses for system call arg1 */
    u8 reg_arg1;
    /* registers Linux uses for system call arg2 */
    u8 reg_arg2;
    /* registers Linux uses for system call arg3 */
    u8 reg_arg3;
    /* ideally, a register not clobbered during syscalls in the ABI for the
     * architecture, to use to store address of current tape cell. If no such
     * register exists, the `arch_funcs->syscall` function must save the value
     * in this register, and restore it after the system call is complete. */
    u8 reg_bf_ptr;

} arch_inter;

/* __BACKENDS__ */
/* this is where the actual interfaces defined in the backend_* files are made
 * available in other files. */
#if BFC_TARGET_X86_64
extern const arch_inter X86_64_INTER;
#endif /* BFC_TARGET_X86_64 */
#if BFC_TARGET_ARM64
extern const arch_inter ARM64_INTER;
#endif /* BFC_TARGET_ARM64 */
#if BFC_TARGET_RISCV64
extern const arch_inter RISCV64_INTER;
#endif /* BFC_TARGET_RISCV64 */
#if BFC_TARGET_S390X
extern const arch_inter S390X_INTER;
#endif /* BFC_TARGET_S390X */

#endif /* BFC_ARCH_INTER_H */
