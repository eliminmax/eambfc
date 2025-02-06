/* SPDX-FileCopyrightText: 2023-2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: 0BSD
 *
 * This program prints the system call numbers for the syscalls used within
 * the eambfc project. It was adapted from a similar tool created for the
 * tiny-clear-elf project (https://github.com/eliminmax/tiny-clear-elf)
 *
 * The intent is for it to be compiled for a new target architecture, then ran
 * through qemu-binfmt, as a quick way to get the system call numbers for that
 * architecture */

/* C99 */
#include <stdio.h>
/* GLIBC */
#include <sys/syscall.h>

int main(void) {
    printf("read:\t%d\n", SYS_read);
    printf("write:\t%d\n", SYS_write);
    printf("exit:\t%d\n", SYS_exit);
    return (0);
}
