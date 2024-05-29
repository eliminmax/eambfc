/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
/* `#include`d for the START_PADDR macro */
#include "eam_compiler_macros.h"


/* reusing these internal functions from eam_compile.c, which are not in the
 * eam_compile.h header file, to create an x86_64 ELF executable that simply
 * calls the Linux exit(1) system call with exit status 0. Attempting to run
 * the resulting binary is a better way to test whether or not the host system
 * can run x86_64 Linux binaries than testing the host architecture and kernel,
 * as setups like QEMU with Linux's binfmt_misc, FreeBSD's Linux syscall
 * emulation, and (possibly) WSL 1 can run Linux x86_64 binaries while having
 * different kernels or architectures. */
int writeEhdr(int fd);
int writePhdrTable(int fd);
int bfExit(int fd);

/* not nearly as robust as the actual code */
int main(int argc, char *argv[]) {
    /* stop GCC complaint when -Wextra is passed */
    (void)argc; (void)argv;
    /* Don't bother worrying about umask for this one. */
    int fd = open("minielf", O_WRONLY+O_CREAT+O_TRUNC, 0755);
    /* Skip over the header space, going straight to writing the code itself. */
    lseek(fd, START_PADDR, SEEK_SET);
    bfExit(fd);
    /* Go back and write the headers */
    lseek(fd, 0, SEEK_SET);
    writeEhdr(fd);
    writePhdrTable(fd);
    return 0;
}
