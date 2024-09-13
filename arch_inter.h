/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains typedefs of structs used to provide the interface to
 * architectures, and declares all supported backends at the end.
 *
 * For information about adding backends, see dev-docs/backends.md */

#ifndef EAMBFC_ARCH_INTER_H
#define EAMBFC_ARCH_INTER_H 1
/* POSIX */
#include "unistd.h" /* off_t */
/* internal */
#include "config.h" /* EAMBFC_TARGET* */
#include "types.h" /* uint*_t, int*_t, bool */

typedef const struct arch_registers {
    /* Linux system call number */
    uint8_t sc_num;
    /* first 3 Linux argument registers */
    uint8_t arg1;
    uint8_t arg2;
    uint8_t arg3;
    /* ideally, a register not clobbered during syscalls in the ABI for the
     * architecture, to use to store address of current tape cell */
    uint8_t bf_ptr;
} arch_registers;

typedef const struct arch_sc_nums {
    /* system call numbers target uses for each of these. */
    int64_t read;
    int64_t write;
    int64_t exit;
} arch_sc_nums;

typedef const struct arch_funcs {
    /* This struct contains function pointers to the functions that are actually
     * used to implement the backend.
     *
     * All functions in here must return true on success, and false on failure.
     * On failure, they should also use functions declared in err.h to print
     * error messages before returning. They must add the size in bytes of the
     * written machine code to *sz right after writing it.
     *
     * Registers are passed to these functions as uint8_t values. They can be
     * the values used within machine code to identify the registers, or they
     * can be numbers that a function not included in this struct can map to
     * register identifiers, depending on how the architecture works. */

    /* General and Register Functions */
    /* Write instruction/s to fd to store immediate imm in register reg. */
    bool (*const set_reg)(uint8_t reg, int64_t imm, int fd, off_t *sz);

    /* Write instruction/s to fd to copy value stored in register src into
     * register dst. */
    bool (*const reg_copy)(uint8_t dst, uint8_t src, int fd, off_t *sz);

    /* Write the system call instruction to fd. */
    bool (*const syscall)(int fd, off_t *sz);

    /* write NOP instruction/s that take the same space as the jump_zero
     * instruction output, to be overwritten once jump_zero is called, but allow
     * for a semi-functional program to analyze if compilation fails due to
     * an unclosed loop. */
    bool (*const nop_loop_open)(int fd, off_t *sz);

    /* Functions that correspond 1 to 1 with brainfuck instructions.
     * Note that the `.` and `,` instructions are implemented using more complex
     * combinations of the above functions, as they involve setting multiple
     * argument registers to either fixed values or the contents of the bf_ptr
     * register, and calling the syscall instruction. */

    /* Write instruction/s to fd to jump offset bytes if the byte stored at
     * address in register reg is set to zero.
     *
     * Used to implement the `[` brainfuck instruction. */
    bool (*const jump_zero)(uint8_t reg, int64_t offset, int fd, off_t *sz);

    /* Write instruction/s to fd to jump <offset> bytes if the byte stored at
     * address in register reg is not set to zero.
     *
     * Used to implement the `]` brainfuck instruction. */
    bool (*const jump_not_zero)(uint8_t reg, int64_t offset, int fd, off_t *sz);

    /* Write instruction/s to fd to increment register reg by one.
     *
     * Used to implement the `>` brainfuck instruction. */
    bool (*const inc_reg)(uint8_t reg, int fd, off_t *sz);

    /* Write instruction/s to fd to decrement register reg by one.
     *
     * Used to implement the `<` brainfuck instruction. */
    bool (*const dec_reg)(uint8_t reg, int fd, off_t *sz);

    /* Write instruction/s to fd to increment byte stored at address in register
     * reg by one.
     *
     * Used to implement the `+` brainfuck instruction. */
    bool (*const inc_byte)(uint8_t reg, int fd, off_t *sz);

    /* Write instruction/s to fd to decrement byte stored at address in register
     * reg by one.
     *
     * Used to implement the `-` brainfuck instruction. */
    bool (*const dec_byte)(uint8_t reg, int fd, off_t *sz);

    /* functions used for optimized instructions */

    /* Write instruction/s to fd to add imm to register reg.
     *
     * Used to implement sequences of consecutive `>` brainfuck instructions. */
    bool (*const add_reg)(uint8_t reg, int64_t imm, int fd, off_t *sz);

    /* Write instruction/s to fd to subtract imm from register reg.
     *
     * Used to implement sequences of consecutive `<` brainfuck instructions. */
    bool (*const sub_reg)(uint8_t reg, int64_t imm, int fd, off_t *sz);

    /* Write instruction/s to fd to add imm8 to byte stored at address in
     * register reg.
     *
     * Used to implement sequences of consecutive `+` brainfuck instructions. */
    bool (*const add_byte)(uint8_t reg, int8_t imm8, int fd, off_t *sz);

    /* Write instruction/s to fd to subtract imm8 from byte stored at address in
     * register reg.
     *
     * Used to implement sequences of consecutive `-` brainfuck instructions. */
    bool (*const sub_byte)(uint8_t reg, int8_t imm8, int fd, off_t *sz);

    /* Write instruction/s to fd to set the value of byte stored at address in
     * register reg to 0.
     *
     * Used to implement the `[-]` and `[+]` brainfuck instruction sequences. */
    bool (*const zero_byte)(uint8_t reg, int fd, off_t *sz);
} arch_funcs;

/* This struct contains all architecture-specific information needed for eambfc,
 * and can be passed as an argument to functions. */
typedef const struct arch_inter {
    arch_funcs *FUNCS;
    arch_sc_nums *SC_NUMS;
    arch_registers *REGS;
    /* CPU flags that should be set for executables for this architecture. */
    uint32_t FLAGS;
    /* The 16-bit EM_* identifier for the architecture, from elf.h */
    uint16_t ELF_ARCH;
    /* Either ELFDATA2LSB or ELFDATA2MSB, depending on byte ordering of the
     * target architecture. */
    unsigned char ELF_DATA;
} arch_inter;

extern const arch_inter X86_64_INTER;
#if EAMBFC_TARGET_ARM64
extern const arch_inter ARM64_INTER;
#endif /* EAMBFC_TARGET_ARM64 */

# endif /* EAMBFC_ARCH_INTER_H */
