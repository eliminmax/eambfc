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

int main(void) {
    printf("read:\t%d\n", SYS_read);
    printf("write:\t%d\n", SYS_write);
    printf("exit:\t%d\n", SYS_exit);
    return (0);
}
