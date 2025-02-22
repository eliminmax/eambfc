/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains typedefs of structs used to provide the interface to
 * architectures, and declares all supported backends at the end. */

#ifndef BFC_ARCH_INTER_H
#define BFC_ARCH_INTER_H 1
/* internal */
#include "config.h" /* BFC_TARGET* */
#include "types.h" /* [iu]{8,16,32,64}, bool, sized_buf */

/* This defines the interface for the architecture. It is written around
 * assumptions that are true about Linux, namely that the system call number and
 * system call arguments are stored in registers.
 *
 * It also assumes that there is either at least one register that is not
 * clobbered during system calls, or that there is a stack that registers can be
 * pushed to or popped from without needing any extra setup. */

/* Once an interface is defined and implemented, the next steps are as follows:
 * 0. Restore the e_machine value to compat/elf.h from GLIBC's elf.h
 * 1. Add a target for that backend to the Makefile
 * 2. Near the top of the Makefile, add it to the BACKENDS variable
 * 3. Add the backend source file to the UNIBUILD_FILES variable in the Makefile
 * 4. In config.template.h, define a macro starting with BFC_TARGET_ to act as a
 *    feature switch for your backend. To disable by default, set it to 0.
 * 5. At the bottom of this file, add a block which declares your interface if
 *    your BFC_TARGET_ macro is set to a non-zero value.
 * 6. Add your backend to the validation block at the start of main.c.
 * 7. Add arguments to select your architecture to the help text and the
 *    argument parsing logic, both in main.c
 * 8. Add it to the list of architectures to test with ubsan in release.sh
 *
 * All of the places that need to be edited have the text __BACKENDS__ in a
 * comment that's right before them, to make it easier to find them, except for
 * within compat/elf.h.
 *
 * For all of these steps, It's best to copy the ARM64 backend and change the
 * ARM64 and AARCH64 identifiers out for your backend, for the sake of
 * consistency. */

typedef const struct {
    /* register Linux checks for system call number */
    u8 sc_num;
    /* registers for first 3 Linux argument registers */
    u8 arg1;
    u8 arg2;
    u8 arg3;
    /* ideally, a register not clobbered during syscalls in the ABI for the
     * architecture, to use to store address of current tape cell. If no such
     * register exists, the arch_funcs->syscall function must push this register
     * to a stack, then pop it once syscall is complete. */
    u8 bf_ptr;
} arch_registers;

typedef const struct {
    /* system call numbers target platform uses for each of these. */
    i64 read;
    i64 write;
    i64 exit;
} arch_sc_nums;

typedef const struct {
    /* This struct contains function pointers to the functions that are actually
     * used to implement the backend.
     *
     * All functions in here must return true on success, and false on failure.
     * On failure, they should also use functions declared in err.h to print
     * error messages before returning.
     *
     * Registers are passed to these functions as u8 values. They can be
     * the values used within machine code to identify the registers, or they
     * can be numbers that a function not included in this struct can map to
     * register identifiers, depending on how the architecture works. */

    /* General and Register Functions */
    /* Write instruction/s to dst_buf to store immediate imm in register reg. */
    bool (*const set_reg)(u8 reg, i64 imm, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to copy value stored in register src into
     * register dst. */
    bool (*const reg_copy)(u8 dst, u8 src, sized_buf *dst_buf);

    /* Write the system call instruction to dst_buf. */
    bool (*const syscall)(sized_buf *dst_buf);

    /* write NOP instruction/s that take the same space as the jump_zero
     * instruction output, to be overwritten once jump_zero is called, but allow
     * for a semi-functional program to analyze if compilation fails due to
     * an unclosed loop. */
    bool (*const nop_loop_open)(sized_buf *dst_buf);

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
    bool (*const inc_reg)(u8 reg, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to decrement register reg by one.
     *
     * Used to implement the `<` brainfuck instruction. */
    bool (*const dec_reg)(u8 reg, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to increment byte stored at address in
     * register reg by one.
     *
     * Used to implement the `+` brainfuck instruction. */
    bool (*const inc_byte)(u8 reg, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to decrement byte stored at address in
     * register reg by one.
     *
     * Used to implement the `-` brainfuck instruction. */
    bool (*const dec_byte)(u8 reg, sized_buf *dst_buf);

    /* functions used for optimized instructions */

    /* Write instruction/s to dst_buf to add imm to register reg.
     *
     * Used to implement sequences of consecutive `>` brainfuck instructions. */
    bool (*const add_reg)(u8 reg, u64 imm, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to subtract imm from register reg.
     *
     * Used to implement sequences of consecutive `<` brainfuck instructions. */
    bool (*const sub_reg)(u8 reg, u64 imm, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to add imm8 to byte stored at address in
     * register reg.
     *
     * Used to implement sequences of consecutive `+` brainfuck instructions. */
    bool (*const add_byte)(u8 reg, u8 imm8, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to subtract imm8 from byte stored at
     * address in register reg.
     *
     * Used to implement sequences of consecutive `-` brainfuck instructions. */
    bool (*const sub_byte)(u8 reg, u8 imm8, sized_buf *dst_buf);

    /* Write instruction/s to dst_buf to set the value of byte stored at address
     * in register reg to 0.
     *
     * Used to implement the `[-]` and `[+]` brainfuck instruction sequences. */
    bool (*const zero_byte)(u8 reg, sized_buf *dst_buf);
} arch_funcs;

/* This struct contains all architecture-specific information needed for eambfc,
 * and can be passed as an argument to functions. */
typedef const struct {
    arch_funcs *FUNCS;
    arch_sc_nums *SC_NUMS;
    arch_registers *REGS;
    /* CPU flags that should be set for executables for this architecture. */
    u32 FLAGS;
    /* The 16-bit EM_* identifier for the architecture, from elf.h */
    u16 ELF_ARCH;
    /* Either ELFDATA2LSB or ELFDATA2MSB, depending on byte ordering of the
     * target architecture. */
    unsigned char ELF_DATA;
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
