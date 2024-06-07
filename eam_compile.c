/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains the implementation of the actual compilation process. */

/* C99 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* POSIX */
#include <fcntl.h>
#include <unistd.h>
/* internal */
#include "compat/elf.h"
#include "config.h"
#include "eam_compiler_macros.h"
#include "eambfc_types.h"
#include "optimize.h"
#include "serialize.h"

off_t codesize;

char instr;
unsigned int instr_line, instr_col;

BFCompilerError err_list[MAX_ERROR];

/* index of the current error in the error list */
err_index_t err_ind;

void resetErrors(void) {
    /* reset error list */
    for(err_index_t i = 0; i < MAX_ERROR; i++) {
        err_list[i].line = 1;
        err_list[i].col = 0;
        err_list[i].err_id = "";
        err_list[i].err_msg = "";
        err_list[i].instr = '\0';
        err_list[i].active = false;
    }
    err_ind = 0;
}

bool optimized;

void appendError(char *error_msg, char *err_id) {
    uint8_t i = err_ind++;
    /* Ensure i is in bounds; discard errors after MAX_ERROR */
    if (i < MAX_ERROR) {
        err_list[i].err_msg = error_msg;
        err_list[i].err_id = err_id;
        err_list[i].instr = instr;
        err_list[i].line = instr_line;
        err_list[i].col = instr_col;
        err_list[i].active = true;
    }
}

bool writeBytes(int fd, const void * bytes, ssize_t expected_size) {
    ssize_t written = write(fd, bytes, expected_size);
    if (written != expected_size) {
        appendError("Failed to write instruction bytes", "FAILED_WRITE");
        return false;
    }
    return true;
}

/* Write the ELF header to the file descriptor fd. */
bool writeEhdr(int fd) {

    /* The format of the ELF header is well-defined and well-documented
     * elsewhere. The struct for it is defined in compat/elf.h, as are most
     * of the values used in here. */

    Elf64_Ehdr header;
    char header_bytes[EHDR_SIZE];

    /* the first 4 bytes are "magic values" that are pre-defined and used to
     * identify the format. */
    header.e_ident[EI_MAG0] = ELFMAG0; header.e_ident[EI_MAG1] = ELFMAG1;
    header.e_ident[EI_MAG2] = ELFMAG2; header.e_ident[EI_MAG3] = ELFMAG3;

    /* x86_64 is a 64-bit architecture. it uses 2's complement, little endian
     * for byte ordering. */
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;

    /* e_ident[EI_VERSION] must be set to EV_CURRENT. */
    header.e_ident[EI_VERSION] = EV_CURRENT;

    /* EI_OSABI is the target Application Binary Interface. SYSV is the value
     * to use for a Linux executable which doesn't use GNU extensions. */
    header.e_ident[EI_OSABI] = ELFOSABI_SYSV;

    /* No distinct ABI versions are defined for ELFOSABI_SYSV. */
    header.e_ident[EI_ABIVERSION] = 0;

    /* The rest of the e_ident bytes are padding bytes.
     * EI_PAD is the index of the first padding byte.
     * EI_NIDENT is the size of the e_ident byte array.
     * Padding bytes are supposed to be zeroed out. */
    for (int i = EI_PAD; i < EI_NIDENT; i++) {
        header.e_ident[i] = 0;
    }

    /* this is a basic executable for the AMD x86_64 architecture. */
    header.e_type = ET_EXEC; header.e_machine = EM_X86_64;
    /* e_version, like e_ident[EI_VERSION], must be set to EV_CURRENT */
    header.e_version = EV_CURRENT;

    /* the remaining parts of the ELF header are defined in a different order
     * than their ordering within the struct, because I believe it's easier
     * to make sense of them in this order. */

    /* the number of program and section table entries, respectively */
    header.e_phnum = PHNUM;
    header.e_shnum = SHNUM;

    /* The offset within the file for the program and section header tables
     * respectively. Defined in macros earlier in eam_compiler_macros.h. */
    header.e_phoff = PHOFF;
    header.e_shoff = SHOFF;

    /* the size of the ELF header as a value within the ELF header, for some
     * reason. I don't make the rules about the format. */
    header.e_ehsize = EHDR_SIZE;

    /* e_phentsize and e_shentsize are the size of entries within the
     * program and section header tables respectively. If there are no entries
     * within a given table, the size should be set to 0. */
    header.e_phentsize = PHDR_SIZE;
    header.e_shentsize = 0;

    /* Section header string table index - the index of the entry in the
     * section header table pointing to the names of each section.
     * Because no such section exists, set it to SHN_UNDEF. */
    header.e_shstrndx = SHN_UNDEF;

    /* e_entry is the virtual memory address of the program's entry point -
     * (i.e. the first instruction to execute). */
    header.e_entry = START_VADDR;

    /* e_flags has a processor-specific meaning. For x86_64, no values are
     * defined, and it should be set to 0. */
    header.e_flags = 0;

    serializeEhdr64(&header, header_bytes);
    return writeBytes(fd, header_bytes, EHDR_SIZE);
}

/* Write the Program Header Table to the file descriptor fd
 * This is a list of areas within memory to set up when starting the program. */
bool writePhdrTable(int fd) {
    Elf64_Phdr phdr_table[PHNUM];
    char phdr_table_bytes[PHTB_SIZE];

    /* header for the tape contents section */
    phdr_table[0].p_type = PT_LOAD;
    /* It is readable and writable */
    phdr_table[0].p_flags = PF_R | PF_W;
    /* Load initial bytes from this offset within the file */
    phdr_table[0].p_offset = 0;
    /* Start at this memory address */
    phdr_table[0].p_vaddr = TAPE_ADDRESS;
    /* Load from this physical address */
    phdr_table[0].p_paddr = 0;
    /* Size within the file on disk - 0, as the tape is empty. */
    phdr_table[0].p_filesz = 0;
    /* Size within memory - must be at least p_filesz.
     * In this case, it's the size of the tape itself. */
    phdr_table[0].p_memsz = TAPE_SIZE;
    /* supposed to be a power of 2, went with 2^12 */
    phdr_table[0].p_align = 0x1000;

    /* header for the segment that contains the actual binary */
    phdr_table[1].p_type = PT_LOAD;
    /* It is readable and executable */
    phdr_table[1].p_flags = PF_R | PF_X;
    /* Load initial bytes from this offset within the file */
    phdr_table[1].p_offset = 0;
    /* Start at this memory address */
    phdr_table[1].p_vaddr = LOAD_VADDR;
    /* Load from this physical address */
    phdr_table[1].p_paddr = 0;
    /* Size within the file on disk - the size of the whole file, as this
     * segment contains the whole thing. */
    phdr_table[1].p_filesz = FILE_SIZE;
    /* size within memory - must be at least p_filesz.
     * In this case, it's the size of the whole file, as the whole file is
     * loaded into this segment */
    phdr_table[1].p_memsz = FILE_SIZE;
    /* supposed to be a power of 2, went with 2^0 */
    phdr_table[1].p_align = 1;

    for (int i = 0; i < PHNUM; i++) {
        serializePhdr64(&phdr_table[i], &(phdr_table_bytes[i * PHDR_SIZE]));
    }
    return writeBytes(fd, phdr_table_bytes, PHTB_SIZE);
}

/* write code to set up a new brainfuck program to the file descriptor fd. */
bool bfInit(int fd) {
    /* set the brainfuck pointer register to the start of the tape  */
    uint8_t instructionBytes[] = {
        eamAsmSetRegD(REG_BF_POINTER, TAPE_ADDRESS)
    };
    codesize += sizeof(instructionBytes);
    return writeBytes(fd, instructionBytes, sizeof(instructionBytes));
}

/* the INC and DEC instructions are encoded very similarly, and share opcodes.
 * the brainfuck instructions "<", ">", "+", and "-" can all be implemented
 * with a few variations of them, using the eamAsmOffset macro. */
bool bfOffset(int fd, uint8_t direction, uint8_t addressMode) {
    uint8_t instructionBytes[] = {
        eamAsmOffset(direction, addressMode, REG_BF_POINTER)
    };
    codesize += sizeof(instructionBytes);
    return writeBytes(fd, instructionBytes, sizeof(instructionBytes));
}

/* The brainfuck instructions "." and "," are similar from an implementation
 * perspective. Both require making system calls for I/O, and the system calls
 * have 3 nearly identical arguments:
 *  - arg1 is the file descriptor
 *  - arg2 is the memory address of the data source (write)/dest (read)
 *  - arg3 is the number of bytes to write/read
 *
 * Due to their similarity, ',' and '.' are both implemented with bfIO. */
bool bfIO(int fd, int bf_fd, int sc) {
    /* bf_fd is the brainfuck File Descriptor, not to be confused with fd,
     * the file descriptor of the output file.
     * sc is the system call number for the system call to use */
    uint8_t instructionBytes[] = {
        /* load the number for the write system call into REG_SC_NUM */
        eamAsmSetRegD(REG_SC_NUM, sc),
        /* load the number for the stdout file descriptor into REG_ARG1 */
        eamAsmSetRegD(REG_ARG1, bf_fd),
        /* copy the address in REG_BF_POINTER to REG_ARG2 */
        eamAsmRegCopy(REG_ARG2, REG_BF_POINTER),
        /* load number of bytes to read/write (1, specifically) into REG_ARG3 */
        eamAsmSetRegD(REG_ARG3, 1),
        /* perform a system call */
        eamAsmSyscall()
    };
    codesize += sizeof(instructionBytes);
    return writeBytes(fd, instructionBytes, sizeof(instructionBytes));
}


struct stack {
    jump_index_t index;
    off_t addresses[MAX_NESTING_LEVEL];
} JumpStack;


/* prepare to compile the brainfuck `[` instruction to file descriptor fd.
 * doesn't actually write to the file yet, as
 * */
bool bfJumpOpen (int fd) {
    off_t expectedLocation;
    /* calculate the expected locationto seek to */
    expectedLocation = (CURRENT_ADDRESS + JUMP_SIZE);
    /* ensure that there are no more than the maximum nesting level */
    if (JumpStack.index + 1 == MAX_NESTING_LEVEL) {
        appendError(
            "Too many nested loops!",
            "OVERFLOW"
        );
        return 0;
    }
    /* push the current address onto the stack */
    JumpStack.addresses[JumpStack.index++] = CURRENT_ADDRESS;
    /* skip enough bytes to write the instruction, once we know where the
     * jump should be to. */
    /* still need to increase codesize for accuracy of the CURRENT_ADDRESS */
    codesize += JUMP_SIZE;
    return lseek(fd, JUMP_SIZE, SEEK_CUR) == expectedLocation;
}

/* compile matching `[` and `]` instructions
 * called when `]` is the instruction to be compiled */
bool bfJumpClose(int fd) {
    off_t openAddress, closeAddress;
    int32_t distance;

    /* ensure that the current index is in bounds */
    if (--JumpStack.index < 0) {
        appendError(
            "Found `]` without matching `[`!",
            "UNMATCHED_CLOSE"
        );
        return 0;
    }
    /* pop the matching `[` instruction's location */
    openAddress = JumpStack.addresses[JumpStack.index];
    closeAddress = CURRENT_ADDRESS;

    distance = (int32_t) (closeAddress - openAddress);

    uint8_t openJumpBytes[] = {
        /* if the current loop is done, jump past the closing check */
        eamAsmJZ(REG_BF_POINTER, distance)
    };
    /* jump to the skipped `[` instruction, write it, and jump back */
    if (lseek(fd, openAddress, SEEK_SET) != openAddress) {
        appendError(
            "Failed to return to `[` instruction!",
            "FAILED_SEEK"
        );
        return 0;
    }
    if (write(fd, openJumpBytes, JUMP_SIZE) != JUMP_SIZE) {
        appendError(
            "Failed to compile `[` instruction!",
            "FAILED_WRITE"
        );
        return 0;
    }
    if (lseek(fd, closeAddress, SEEK_SET) != closeAddress) {
        appendError(
            "Failed to return to `]` instruction!",
            "FAILED_SEEK"
        );
        return 0;
    }
    uint8_t instructionBytes[] = {
        /* jump to right after the `[` instruction, to skip a redundant check */
        eamAsmJNZ(REG_BF_POINTER, -distance)
    };
    codesize += sizeof(instructionBytes);
    return writeBytes(fd, instructionBytes, sizeof(instructionBytes));
}

/* compile an individual instruction (c), to the file descriptor fd.
 * passes fd along with the appropriate arguments to a function to compile that
 * particular instruction */
bool bfCompileInstruction(char c, int fd) {
    bool ret;
    instr = c;
    instr_col++;
    switch(c) {
        case '<':
            /* decrement the tape pointer register */
            ret = bfOffset(fd, OFFDIR_NEGATIVE, OFFMODE_REGISTER);
            break;
        case '>':
            /* increment the tape pointer register */
            ret = bfOffset(fd, OFFDIR_POSITIVE, OFFMODE_REGISTER);
            break;
        case '+':
            /* increment the current tape value */
            ret = bfOffset(fd, OFFDIR_POSITIVE, OFFMODE_MEMORY);
            break;
        case '-':
            /* decrement the current tape value */
            ret = bfOffset(fd, OFFDIR_NEGATIVE, OFFMODE_MEMORY);
            break;
        case '.':
            /* write to stdout */
            ret = bfIO(fd, STDOUT_FILENO, SYSCALL_WRITE);
            break;
        case ',':
            /* read from stdin */
            ret = bfIO(fd, STDIN_FILENO, SYSCALL_READ);
            break;
        case '[':
            ret = bfJumpOpen(fd);
            break;
        case ']':
            ret = bfJumpClose(fd);
            break;
        case '\n':
            /* add 1 to the line number and reset the column. */
            instr_line++;
            instr_col = 0;
            ret = true;
            break;
        default:
            /* any other characters are comments, silently continue. */
            ret = true;
            break;
    }
    return ret;
}

/* write code to perform the exit(0) syscall */
bool bfExit(int fd) {
    uint8_t instructionBytes[] = {
        /* set system call register to exit system call number */
        eamAsmSetRegD(REG_SC_NUM, SYSCALL_EXIT),
        /* set system call register to the desired exit code (0) */
        eamAsmSetRegD(REG_ARG1, 0),
        /* perform a system call */
        eamAsmSyscall()
    };
    codesize += sizeof(instructionBytes);
    return writeBytes(fd, instructionBytes, sizeof(instructionBytes));
}


bool bfCleanup(int fd) {
    int ret = bfExit(fd);
    /* Ehdr and Phdr table are at the start */
    lseek(fd, 0, SEEK_SET);
    /* a |= b means a = (a | b) */
    ret |= writeEhdr(fd);
    ret |= writePhdrTable(fd);
    return ret;
}

/* maximum number of bytes to transfer from tmpfile at a time */
#define MAX_TRANS_SZ 1024
/* Takes 2 open file descriptors - in_fd and out_fd.
 * in_fd is a brainfuck source file, open for reading.
 * outputFS is the destination file, open for writing.
 * It compiles the source code in in_fd, writing the output to out_fd.
 *
 * It does not verify that in_fd and out_fd are valid file descriptors,
 * nor that they are open properly.
 *
 * It calls several other functions to compile the source code. If any of
 * them return a falsy value, it aborts, returning 0.
 *
 * If all of the other functions succeeded, it returns 1. */
int bfCompile(int in_fd, int out_fd, bool optimize) {
    /* TODO: use optimize */
    optimized = optimize;
    /* allow compiling with -Werror -Wall -Wextra before optimize is used */
    (void) optimize;
    int ret = 1;
    FILE *tmp_file = tmpfile();
    if (tmp_file == NULL) {
        appendError("Could not open a tmpfile.", "FAILED_TMPFILE");
        return 0;
    }
    int tmp_fd = fileno(tmp_file);
    if (tmp_fd == -1) {
        appendError(
            "Could not get file descriptor for tmpfile",
            "FAILED_TMPFILE"
        );
        return 0;
    }
    /* reset codesize variable used in several macros in eam_compiler_macros */
    codesize = 0;
    /* reset the jump stack for the new file */
    JumpStack.index = 0;
    /* reset the error stack for the new file */
    resetErrors();
    /* reset the current line and column */
    instr_line = 1;
    instr_col = 0;

    /* skip the headers until we know the code size */
    if (fseek(tmp_file, START_PADDR, SEEK_SET) != 0) {
        appendError("Failed to seek to start of code.", "FAILED_SEEK");
        return 0;
    }

    if (!bfInit(tmp_fd)) {
        appendError(
            "Failed to write initial setup instructions.",
            "FAILED_WRITE"
        );
        ret = 0;
    }

    while (read(in_fd, &instr, 1)) {
        /* the appropriate error message(s) are already appended */
        if (!bfCompileInstruction(instr, tmp_fd)) ret = 0;
    }

    /* now, code size is known, so we can write the headers
     * the appropriate error message(s) are already appended */
    if (!bfCleanup(tmp_fd)) ret = 0;
    if(JumpStack.index > 0) {
        appendError(
            "Reached the end of the file with an unmatched `[`!",
            "UNMATCHED_OPEN"
        );
        ret = 0;
    }

    if (fseek(tmp_file, 0, SEEK_SET) != 0) {
        appendError("Failed to seek to start of tmpfile.", "FAILED_SEEK");
        ret = 0;
    }

    /* copy tmpfile over to the output file. */

    char trans[MAX_TRANS_SZ];
    ssize_t trans_sz;

    while ((trans_sz = read(tmp_fd, &trans, MAX_TRANS_SZ))) {
        if (trans_sz == -1) {
            appendError("Failed to read bytes from tmpfile", "FAILED_TMPFILE");
            ret = 0;
        } else if ((write(out_fd, &trans, trans_sz) != trans_sz)) {
            appendError("Failed to write bytes from tmpfile", "FAILED_TMPFILE");
            ret = 0;
        }
    }

    fclose(tmp_file);

    return ret;
}
