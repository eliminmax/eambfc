/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * */

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
#include "eam_compiler_macros.h"
#include "elf.h"
#include "serialize.h"

off_t codesize;

char currentInstruction;
unsigned int currentInstructionLine, currentInstructionColumn;

typedef struct {
    unsigned int currentInstructionLine;
    unsigned int currentInstructionColumn;
    char *errorId;
    char *errorMessage;
    char currentInstruction;
    bool active;
} BFCompilerError;

#define MAX_ERROR 32

uint8_t errorIndex;
BFCompilerError ErrorList[MAX_ERROR];

void resetErrors(void) {
    /* reset error list */
    for(int i = 0; i < MAX_ERROR; i++) {
        ErrorList[i].currentInstructionLine = 1;
        ErrorList[i].currentInstructionColumn = 0;
        ErrorList[i].errorId = "";
        ErrorList[i].errorMessage = "";
        ErrorList[i].currentInstruction = '\0';
        ErrorList[i].active = false;
    }
    errorIndex = 0;
}

void appendError(char *errormsg, char *errorId) {
    uint8_t i = errorIndex++;
    /* Ensure i is in bounds; discard errors after MAX_ERROR */
    if (i < MAX_ERROR) {
        ErrorList[i].errorMessage = errormsg;
        ErrorList[i].errorId = errorId;
        ErrorList[i].currentInstruction = currentInstruction;
        ErrorList[i].currentInstructionLine = currentInstructionLine;
        ErrorList[i].currentInstructionColumn = currentInstructionColumn;
        ErrorList[i].active = true;
    }
}

/* Write the ELF header to the file descriptor fd. */
int writeEhdr(int fd) {

    /* The format of the ELF header is well-defined and well-documented
     * elsewhere. The struct for it is defined in elf.h, as are most
     * of the values used in here. */

    Elf64_Ehdr header;
    char headerBytes[EHDR_SIZE];

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

    serializeEhdr64(&header, headerBytes);
    return write(fd, &headerBytes, EHDR_SIZE) == EHDR_SIZE;
}

/* Write the Program Header Table to the file descriptor fd
 * This is a list of areas within memory to set up when starting the program. */
int writePhdrTable(int fd) {
    Elf64_Phdr headerTable[PHNUM];
    char headerTableBytes[PHTB_SIZE];

    /* header for the tape contents section */
    headerTable[0].p_type = PT_LOAD;
    /* It is readable and writable */
    headerTable[0].p_flags = PF_R | PF_W;
    /* Load initial bytes from this offset within the file */
    headerTable[0].p_offset = 0;
    /* Start at this memory address */
    headerTable[0].p_vaddr = TAPE_ADDRESS;
    /* Load from this physical address */
    headerTable[0].p_paddr = 0;
    /* Size within the file on disk - 0, as the tape is empty. */
    headerTable[0].p_filesz = 0;
    /* Size within memory - must be at least p_filesz.
     * In this case, it's the size of the tape itself. */
    headerTable[0].p_memsz = TAPE_SIZE;
    /* supposed to be a power of 2, went with 2^12 */
    headerTable[0].p_align = 0x1000;

    /* header for the segment that contains the actual binary */
    headerTable[1].p_type = PT_LOAD;
    /* It is readable and executable */
    headerTable[1].p_flags = PF_R | PF_X;
    /* Load initial bytes from this offset within the file */
    headerTable[1].p_offset = 0;
    /* Start at this memory address */
    headerTable[1].p_vaddr = LOAD_VADDR;
    /* Load from this physical address */
    headerTable[1].p_paddr = 0;
    /* Size within the file on disk - the size of the whole file, as this
     * segment contains the whole thing. */
    headerTable[1].p_filesz = FILE_SIZE;
    /* size within memory - must be at least p_filesz.
     * In this case, it's the size of the whole file, as the whole file is
     * loaded into this segment */
    headerTable[1].p_memsz = FILE_SIZE;
    /* supposed to be a power of 2, went with 2^0 */
    headerTable[1].p_align = 1;

    for (int i = 0; i < PHNUM; i++) {
        serializePhdr64(&headerTable[i], &(headerTableBytes[i * PHDR_SIZE]));
    }
    return write(fd, &headerTableBytes, PHTB_SIZE) == PHTB_SIZE;
}

/* write code to set up a new brainfuck program to the file descriptor fd. */
int bfInit(int fd) {
    /* set the brainfuck pointer register to the start of the tape  */
    uint8_t instructionBytes[] = {
        eamasm_setregd(REG_BF_POINTER, TAPE_ADDRESS)
    };
    writeInstructionBytes();
}

/* the INC and DEC instructions are encoded very similarly, and share opcodes.
 * the brainfuck instructions "<", ">", "+", and "-" can all be implemented
 * with a few variations of them, using the eamasm_offset macro. */
int bfOffset(int fd, uint8_t direction, uint8_t addressMode) {
    uint8_t instructionBytes[] = {
        eamasm_offset(direction, addressMode, REG_BF_POINTER)
    };
    writeInstructionBytes();
}

/* The brainfuck instructions "." and "," are similar from an implementation
 * perspective. Both require making system calls for I/O, and the system calls
 * have 3 nearly identical arguments:
 *  - arg1 is the file descriptor
 *  - arg2 is the memory address of the data source (write)/dest (read)
 *  - arg3 is the number of bytes to write/read
 *
 * Due to their similarity, ',' and '.' are both implemented with bfIO. */
int bfIO(int fd, int bfFD, int sc) {
    /* bfFD is the brainfuck File Descriptor, not to be confused with FD,
     * the file descriptor of the output file.
     * sc is the system call number for the system call to use */
    uint8_t instructionBytes[] = {
        /* load the number for the write system call into REG_SC_NUM */
        eamasm_setregd(REG_SC_NUM, sc),
        /* load the number for the stdout file descriptor into REG_ARG1 */
        eamasm_setregd(REG_ARG1, bfFD),
        /* copy the address in REG_BF_POINTER to REG_ARG2 */
        eamasm_regcopy(REG_ARG2, REG_BF_POINTER),
        /* load number of bytes to read/write (1, specifically) into REG_ARG3 */
        eamasm_setregd(REG_ARG3, 1),
        /* perform a system call */
        eamasm_syscall()
    };
    writeInstructionBytes();
}

#define MAX_NESTING_LEVEL 64

struct stack {
    int8_t index;
    off_t addresses[MAX_NESTING_LEVEL];
} JumpStack;


/* prepare to compile the brainfuck `[` instruction to file descriptor fd.
 * doesn't actually write to the file yet, as
 * */
int bfJumpOpen (int fd) {
    off_t expectedLocation;
    /* calculate the expected locationto seek to */
    expectedLocation = (CURRENT_ADDRESS + JUMP_SIZE);
    /* ensure that there are no more than the maximun nesting level */
    if (JumpStack.index >= MAX_NESTING_LEVEL) {
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
int bfJumpClose(int fd) {
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
        eamasm_jump_zero(REG_BF_POINTER, distance)
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
        eamasm_jump_not_zero(REG_BF_POINTER, -distance)
    };
    writeInstructionBytes();
}

/* compile an individual instruction (c), to the file descriptor fd.
 * passes fd along with the appropriate arguments to a function to compile that
 * particular instruction */
int bfCompileInstruction(char c, int fd) {
    int ret;
    currentInstruction = c;
    currentInstructionColumn++;
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
            currentInstructionLine++;
            currentInstructionColumn = 0;
            /* Don't break here, as the default should also run. */
        default:
            /* any other characters are comments, silently continue. */
            ret = 1;
            break;
    }
    return ret;
}

/* write code to perform the exit(0) syscall */
int bfExit(int fd) {
    uint8_t instructionBytes[] = {
        /* set system call register to exit system call number */
        eamasm_setregd(REG_SC_NUM, SYSCALL_EXIT),
        /* set system call register to the desired exit code (0) */
        eamasm_setregd(REG_ARG1, 0),
        /* perform a system call */
        eamasm_syscall()
    };
    writeInstructionBytes();
}


int bfCleanup(int fd) {
    int ret = bfExit(fd);
    /* Ehdr and Phdr table are at the start */
    lseek(fd, 0, SEEK_SET);
    /* a |= b means a = (a | b) */
    ret |= writeEhdr(fd);
    ret |= writePhdrTable(fd);
    return ret;
}

/* Takes 2 open file descriptors - inputFD and outputFD.
 * inputFD is a brainfuck source file, open for reading.
 * outputFS is the destination file, open for writing.
 * It compiles the source code in inputFD, writing the output to outputFD.
 *
 * It does not verify that inputFD and outputFD are valid file descriptors,
 * nor that they are open properly.
 *
 * It calls several other functions to compile the source code. If any of
 * them return a falsy value, it aborts, returning 0.
 *
 * If all of the other functions succeeded, it returns 1. */
#define guarded(thing) if(!(thing)) ret = 0
int bfCompile(int inputFD, int outputFD, bool keep){
    char instruction;
    int ret = 1;
    /* reset codesize variable used in several macros in eam_compiler_macros */
    codesize = 0;
    /* reset the jump stack for the new file */
    JumpStack.index = 0;
    /* reset the error stack for the new file */
    resetErrors();
    /* reset the current line and column */
    currentInstructionLine = 1;
    currentInstructionColumn = 0;

    /* skip the headers until we know the code size */
    if (lseek(outputFD, START_PADDR, SEEK_SET) != START_PADDR) {
        appendError(
            "Could not seek to start of code. Output may be a FIFO.",
            "FAILED_SEEK"
        );
        return 0;
    }

    guarded(bfInit(outputFD));

    while (read(inputFD, &instruction, 1)) {
        guarded(bfCompileInstruction(instruction, outputFD));
    }

    guarded(bfCleanup(outputFD));

    /* now, code size is known, so we can write the headers */
    if(JumpStack.index > 0) {
        appendError(
            "Reached the end of the file with an unmatched `[`!",
            "UNMATCHED_OPEN"
        );
        return 0;
    }
    return ret;
}
