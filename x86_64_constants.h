#ifndef EAM_X86_64_CONSTANTS
#define EAM_X86_64_CONSTANTS 1
/* internal */
#include "compat/elf.h"

/* the Linux kernel reads system call numbers from RAX on x86_64 systems,
 * and reads arguments from RDI, RSI, RDX, R10, R8, and R9.
 * None of the system calls needed use more than 3 arguments, and the R8-R15
 * registers are addressed incompatibly, so only worry the first 3 argument
 * registers.
 *
 * the RBX register is preserved through system calls, so it's useful as the
 * tape pointer.
 *
 * Thus, for eambfc, the registers to care about are RAX, RDI, RSI, RDX, and RBX
 *
 * Oversimpifying a bit, in x86 assembly, when specifying a register that is not
 * one of R8-R15, a 3-bit value is used to identify it.
 *
 * * RAX is 000b
 * * RDI is 111b
 * * RSI is 110b
 * * RDX is 010b
 * * RBX is 011b
 *
 * Because one octal symbol represents 3 bits, octal is used for the macro
 * definitions.
 * */
#define REG_SC_NUM      00 /* RAX */
#define REG_ARG1        07 /* RDI */
#define REG_ARG2        06 /* RSI */
#define REG_ARG3        02 /* RDX */
#define REG_BF_PTR      03 /* RBX */

/* in eambfc, `[` and `]` are both compiled to TEST (3 bytes), followed by a Jcc
 * instruction (6 bytes). When encountering a `[` instruction, skip this many
 * bytes to leave room for them. */
#define JUMP_SIZE 9

#define ARCH_FLAGS 0
#define ARCH_EI_DATA ELFDATA2LSB

/* Linux system call numbers on AMD x86_64 */

#define SYSCALL_READ 0
#define SYSCALL_WRITE 1
#define SYSCALL_EXIT 60

#endif /* EAM_X86_64_CONSTANTS */
