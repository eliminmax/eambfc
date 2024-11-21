/* SPDX-FileCopyrightText: 2023-2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: 0BSD
 *
 * This program prints the system call numbers for the syscalls used within
 * the eambfc project. It was adapted from a similar tool created for the
 * tiny-clear-elf project (https://github.com/eliminmax/tiny-clear-elf) */

/* C99 */
#include <stdio.h>
/* POSIX */
#include <sys/syscall.h>
#include <unistd.h>

#define PRINT_SCNUM(sc_name) printf(#sc_name ":\t%d\n", __NR_##sc_name)

int main(void) {
    PRINT_SCNUM(read);
    PRINT_SCNUM(write);
    PRINT_SCNUM(exit);
    return (0);
}
