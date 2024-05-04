/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * */

#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <fcntl.h>
#include <elf.h>
#include "eamasm_macros.h"

ssize_t codesize;

/* Write the ELF header to the file descriptor fd. */
int writeEhdr(int fd) {

    /* The format of the ELF header is well-defined and well-documented
     * elsewhere. The struct for it is defined in elf.h, as are most
     * of the values used in here.*/

    ssize_t written;
    Elf64_Ehdr header;
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
    header.e_type = ET_EXEC;
    header.e_machine = EM_X86_64;
    /* e_version, like e_ident[EI_VERSION], must be set to EV_CURRENT */
    header.e_version = EV_CURRENT;

    /* the remaining parts of the ELF header are defined in a different order
     * than their ordering within the struct, because I believe it's easier
     * to make sense of them in this order. */

    /* the number of program and section table entries, respectively */
    header.e_phnum = PHNUM;
    header.e_shnum = SHNUM;

    /* The offset within the file for the program and section header tables
     * respectively. Defined in macros earlier in this file. */
    header.e_phoff = PHOFF;
    header.e_shoff = SHOFF;

    /* e_phentsize and e_shentsize are the size of entries within the
     * program and section header tables respectively. If there are no entries
     * within a given table, the size should be set to 0. */
    header.e_phentsize = (PHNUM ? sizeof(Elf64_Phdr) : 0);
    header.e_shentsize = (SHNUM ? sizeof(Elf64_Shdr) : 0);

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

    /* the size of the ELF header as a value within the ELF header, for some
     * reason. I don't make the rules about the format. */
    header.e_ehsize = EHDR_SIZE;

    written = write(fd, &header, EHDR_SIZE);
    return written == sizeof(header);
}

/* Write the Program Header Table */
int writePhdrTable(int fd) {
    ssize_t written;
    Elf64_Phdr headerTable[PHNUM];

    /* /1* header for the segment that contains the ELF header *1/ */
    /* /1* just looked at the values from a known-working executable for this one *1/ */
    /* headerTable[0].p_type = PT_PHDR; */
    /* headerTable[0].p_flags = PF_R; */
    /* headerTable[0].p_offset = EHDR_SIZE; */
    /* headerTable[0].p_vaddr = EHDR_SIZE; */
    /* headerTable[0].p_paddr = EHDR_SIZE; */
    /* headerTable[0].p_filesz = PHTB_SIZE; */
    /* headerTable[0].p_memsz = PHTB_SIZE; */
    /* headerTable[0].p_align = 8; */

    /* /1* header for the segment that contains the Phdr table *1/ */
    /* overlaps with the previous one, and similarly copied from a known-working
     * executable, but with some changes */
    /* headerTable[1].p_type = PT_LOAD; */
    /* headerTable[1].p_flags = PF_R; */
    /* headerTable[1].p_offset = 0; */
    /* headerTable[1].p_vaddr = 0; */
    /* headerTable[1].p_paddr = 0; */
    /* headerTable[1].p_filesz = EHDR_SIZE + PHTB_SIZE; */
    /* headerTable[1].p_memsz = EHDR_SIZE + PHTB_SIZE; */
    /* headerTable[1].p_align = 1; */

    /* header for the tape contents section */
    headerTable[0].p_type = PT_LOAD;
    headerTable[0].p_flags = PF_R | PF_W;
    headerTable[0].p_offset = 0;
    headerTable[0].p_vaddr = TAPE_ADDRESS;
    headerTable[0].p_paddr = 0;
    headerTable[0].p_filesz = 0;
    headerTable[0].p_memsz = TAPE_SIZE;
    headerTable[0].p_align = 0x1000;

    /* header for the segment that contains the actual binary */
    headerTable[1].p_type = PT_LOAD;
    headerTable[1].p_flags = PF_R | PF_X;
    headerTable[1].p_offset = 0;
    headerTable[1].p_vaddr = LOAD_VADDR;
    headerTable[1].p_paddr = 0;
    headerTable[1].p_filesz = FILE_SIZE;
    headerTable[1].p_memsz = FILE_SIZE;
    headerTable[1].p_align = 1; /* supposed to be a power of 2, went with 2^0 */

    written = write(fd, &headerTable, PHTB_SIZE);
    return written == PHTB_SIZE;
}

/* Write the Section Header Table */
/* int writeShdrTable(int fd) { */
/*     return 1; */
/*     ssize_t written; */
/*     Elf64_Shdr headerTable[SHNUM]; */
/*     /1* Tape section *1/ */
/*     headerTable[0].sh_name = 0; */
/*     /1* the tape section occupies no space within the executable *1/ */
/*     headerTable[0].sh_type = SHT_NOBITS; */
/*     /1* the tape section should be allocated and writable during execution *1/ */
/*     headerTable[0].sh_flags = SHF_WRITE | SHF_ALLOC; */
/*     /1* the location within the memory image *1/ */
/*     headerTable[0].sh_addr = TAPE_ADDRESS; */
/*     /1* for a SHT_NOBITS-type section, the offset is supposed to be set to its */
/*      * "conceptual placement within the file". I guess that would be within */
/*      * the padding between the Phdr table and the start of the code *1/ */
/*     headerTable[0].sh_offset = PHOFF + PHTB_SIZE; */
/*     /1* The size within the file *1/ */
/*     headerTable[0].sh_size = 0; */
/*     /1* linking information - none needed here, so set it to 0 *1/ */
/*     headerTable[0].sh_link = 0; */
/*     /1* miscellaneous information - none needed here, so set it to 0 *1/ */
/*     headerTable[0].sh_info = 0; */
/*     /1* alignment constraint information *1/ */
/*     headerTable[0].sh_addralign = 0; */
/*     /1* if the section contains "a table of fixed-size entries", this is the size */
/*      * of one of the entries. If it's zero, that means that it does not contain */
/*      * such a table. *1/ */
/*     headerTable[0].sh_entsize = 0; */
/*     /1* Code section *1/ */
/*     headerTable[1].sh_name = 1; */
/*     /1* section contains information defined by the program. *1/ */
/*     headerTable[1].sh_type = SHT_PROGBITS; */
/*     /1* the code section should be allocated, and contains executable machine */
/*      * instructions. *1/ */
/*     headerTable[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR; */
/*     /1* The location within the memory image *1/ */
/*     headerTable[1].sh_addr = START_VADDR; */
/*     /1* The location within the file *1/ */
/*     headerTable[1].sh_offset = START_PADDR; */
/*     /1* The size within the file *1/ */
/*     headerTable[1].sh_size = codesize; */
/*     /1* linking information - none needed here, so set it to 0 *1/ */
/*     headerTable[1].sh_link = 0; */
/*     /1* miscellaneous information - none needed here, so set it to 0 *1/ */
/*     headerTable[1].sh_info = 0; */
/*     /1* alignment constraint information *1/ */
/*     headerTable[1].sh_addralign = 0; */
/*     /1* Set to 0 for same reason as above *1/ */
/*     headerTable[1].sh_entsize = 0; */
/*     written = write(fd, &headerTable, SHTB_SIZE); */
/*     return written == SHTB_SIZE; */
/* } */

int bfInit(int fd) {
    /* set the brainfuck pointer register to the start of the tape  */
    ssize_t written;
    uint8_t instructionBytes[] = {
        eamasm_setregd(REG_BF_POINTER, TAPE_ADDRESS)
    };
    written = write(fd, &instructionBytes, sizeof(instructionBytes));
    codesize += written;
    return written == sizeof(instructionBytes);
}

/* the INC and DEC instructions are encoded very similarly, and share opcodes.
 * the brainfuck instructions "<", ">", "+", and "-" can all be implemented
 * with a few variations. */
int bfOffset(int fd, uint8_t direction, uint8_t addressMode) {
    ssize_t written;
    uint8_t instructionBytes[] = {
        eamasm_offset(direction, addressMode, REG_BF_POINTER)
    };
    written = write(fd, &instructionBytes, sizeof(instructionBytes));
    codesize += written;
    return written == sizeof(instructionBytes);
}

/* The brainfuck instructions "." and "," are similar from an implementation
 * perspective. Both require making system calls for I/O, and the system calls
 * have 3 nearly identical arguments:
 *  - arg1 is the file descriptor
 *  - arg2 is the memory address of the the data source (write)/dest (read)
 *  - arg3 is the number of bytes to write/read
 *
 *  They are both implemented with the bfIO function.
 * */
int bfIO(int fd, int bfFD, int sc) {
    /* bfFD is the brainfuck File Descriptor, not to be confused with FD,
     * the file descriptor of the output file.
     * sc is the system call number for the system call to use
     * */
    ssize_t written;
    uint8_t instructionBytes[] = {
        /* load the number for the write system call into REG_SC_NUM */
        eamasm_setregd(REG_SC_NUM, sc),
        /* load the number for the stdout file descriptor into REG_ARG1 */
        eamasm_setregd(REG_ARG1, bfFD),
        /* copy the address in REG_BF_POINTER to REG_ARG2 */
        eamasm_regcopy(REG_ARG2, REG_BF_POINTER),
        /* load the number of bytes to write (1, specifically) into REG_ARG3 */
        eamasm_setregd(REG_ARG3, 1),
        /* perform a system call */
        eamasm_syscall()
    };
    written = write(fd, &instructionBytes, sizeof(instructionBytes));
    codesize += written;
    return written == sizeof(instructionBytes);
}

int bfJmp(int fd) {
    fputs("\"[\" and \"]\" are not yet implemented.\n", stderr);
    return 0;
}

/* compile an individual instruction (c), to the file descriptor fd. */
int bfCompileInstruction(char c, int fd) {
    int ret;
    switch(c) {
        case '<':
            ret = bfOffset(fd, OFFDIR_NEGATIVE, OFFMODE_REGISTER);
            break;
        case '>':
            ret = bfOffset(fd, OFFDIR_POSITIVE, OFFMODE_REGISTER);
            break;
        case '+':
            ret = bfOffset(fd, OFFDIR_POSITIVE, OFFMODE_MEMORY);
            break;
        case '-':
            ret = bfOffset(fd, OFFDIR_NEGATIVE, OFFMODE_MEMORY);
            break;
        case '.':
            ret = bfIO(fd, STDOUT_FILENO, SYS_write);
            break;
        case ',':
            ret = bfIO(fd, STDIN_FILENO, SYS_read);
            break;
        case '[':
            ret = bfJmp(fd);
            break;
        case ']':
            ret = bfJmp(fd);
            break;
        default:
            /* any other characters are comments, silently continue. */
            ret = 1;
            break;
    }
    return ret;
}

/* write code to perform the exit(0) syscall */
int bfCleanup(int fd) {
    ssize_t written;
    uint8_t instructionBytes[] = {
        /* set system call register to exit system call number */
        eamasm_setregd(REG_SC_NUM, SYS_exit),
        /* set system call register to the desired exit code (0) */
        eamasm_setregd(REG_ARG1, 0),
        /* perform a system call */
        eamasm_syscall()
    };
    written = write(fd, &instructionBytes, sizeof(instructionBytes));
    codesize += written;
    return written == sizeof(instructionBytes);
}

/* macro to remove repeated guards */
#define guarded(thing) if(!thing) return 0
/* Takes 2 open file descriptors - inputFD and outputFD.
 * inputFD is a brainfuck source file, open for reading.https://www.muppetlabs.com/~breadbox/software/elfkickers.html
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
int bfCompile(int inputFD, int outputFD){
    char instruction;
    codesize = 0;
    /* skip the headers until we know the code size */
    lseek(outputFD, START_PADDR, SEEK_SET);

    guarded(bfInit(outputFD));

    while (read(inputFD, &instruction, 1)) {
        guarded(bfCompileInstruction(instruction, outputFD));
    }
    guarded(bfCleanup(outputFD));

    /* now, code size is known, so we can write the headers */
    /* Ehdr and Phdr table are at the start */
    lseek(outputFD, 0, SEEK_SET);
    guarded(writeEhdr(outputFD));
    guarded(writePhdrTable(outputFD));
    /* /1* Shdr table is at the end *1/ */
    /* lseek(outputFD, SHOFF, SEEK_SET); */
    /* guarded(writeShdrTable(outputFD)); */
    return 1;
}
