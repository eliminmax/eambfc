/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains the implementation of the actual compilation process.
 * It is by far the most significant part of the EAMBFC codebase. */

/* C99 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
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
#include "err.h"
#include "optimize.h"
#include "serialize.h"
#include "x86_64_encoders.h"

static off_t out_sz;

static char instr;

static inline bool writeBytes(int fd, const void *bytes, ssize_t sz) {
    ssize_t written = write(fd, bytes, sz);
    if (written != sz) {
        appendError(instr, "Failed to write instruction bytes", "FAILED_WRITE");
        return false;
    }
    return true;
}

/* Write the ELF header to the file descriptor fd. */
static bool writeEhdr(int fd) {

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
static bool writePhdrTable(int fd) {
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

/* The brainfuck instructions "." and "," are similar from an implementation
 * perspective. Both require making system calls for I/O, and the system calls
 * have 3 nearly identical arguments:
 *  - arg1 is the file descriptor
 *  - arg2 is the memory address of the data source (write)/dest (read)
 *  - arg3 is the number of bytes to write/read
 *
 * Due to their similarity, ',' and '.' are both implemented with bfIO. */
static bool bfIO(int fd, int bf_fd, int sc) {
    /* bf_fd is the brainfuck File Descriptor, not to be confused with fd,
     * the file descriptor of the output file.
     * sc is the system call number for the system call to use */
    /* load the number for the write system call into REG_SC_NUM */
    bool ret = eamAsmSetReg(REG_SC_NUM, sc, fd, &out_sz);
    /* load the number for the stdout file descriptor into REG_ARG1 */
    ret &= eamAsmSetReg(REG_ARG1, bf_fd, fd, &out_sz);
    /* copy the address in REG_BF_PTR to REG_ARG2 */
    ret &= eamAsmRegCopy(REG_ARG2, REG_BF_PTR, fd, &out_sz);
    /* load number of bytes to read/write (1, specifically) into REG_ARG3 */
    ret &= eamAsmSetReg(REG_ARG3, 1, fd, &out_sz);
    ret &= eamAsmSyscall(fd, &out_sz);
    return ret;
}

static struct stack {
    jump_index_t index;
    off_t addresses[MAX_NESTING_LEVEL];
} JumpStack;

/* prepare to compile the brainfuck `[` instruction to file descriptor fd.
 * doesn't actually write to the file yet, as the address of `]` is unknown. */
static bool bfJumpOpen (int fd) {
    off_t expectedLocation;
    /* calculate the expected locationto seek to */
    expectedLocation = (CURRENT_ADDRESS + JUMP_SIZE);
    /* ensure that there are no more than the maximum nesting level */
    if (JumpStack.index + 1 == MAX_NESTING_LEVEL) {
        appendError(
            instr,
            "Too many nested loops!",
            "OVERFLOW"
        );
        return false;
    }
    /* push the current address onto the stack */
    JumpStack.addresses[JumpStack.index++] = CURRENT_ADDRESS;
    /* skip enough bytes to write the instruction, once we know where the
     * jump should be to. */
    /* still need to increase out_sz for accuracy of the CURRENT_ADDRESS */
    out_sz += JUMP_SIZE;
    return lseek(fd, JUMP_SIZE, SEEK_CUR) == expectedLocation;
}

/* compile matching `[` and `]` instructions
 * called when `]` is the instruction to be compiled */
static bool bfJumpClose(int fd) {
    off_t openAddress, closeAddress;
    int32_t distance;

    /* ensure that the current index is in bounds */
    if (--JumpStack.index < 0) {
        appendError(
            instr,
            "Found `]` without matching `[`!",
            "UNMATCHED_CLOSE"
        );
        return false;
    }
    /* pop the matching `[` instruction's location */
    openAddress = JumpStack.addresses[JumpStack.index];
    closeAddress = CURRENT_ADDRESS;

    distance = closeAddress - openAddress;

    /* jump to the skipped `[` instruction, write it, and jump back */
    if (lseek(fd, openAddress, SEEK_SET) != openAddress) {
        appendError(
            instr,
            "Failed to return to `[` instruction!",
            "FAILED_SEEK"
        );
        return false;
    }
    off_t phony = 0; /* already added to code size for this one */
    if (!eamAsmJumpZero(REG_BF_PTR, distance, fd, &phony)) {
        appendError(
            instr,
            "Failed to write `[` instruction!",
            "FAILED_WRITE"
        );
        return false;
    }

    if (lseek(fd, closeAddress, SEEK_SET) != closeAddress) {
        appendError(
            instr,
            "Failed to return to `]` instruction!",
            "FAILED_SEEK"
        );
        return false;
    }
    /* jump to right after the `[` instruction, to skip a redundant check */
    if (!eamAsmJumpNotZero(REG_BF_PTR, -distance, fd, &out_sz)) {
        appendError(instr, "Failed to write `]` instruction", "FAILED_WRITE");
        return false;
    }

    return true;
}

/* compile an individual instruction (c), to the file descriptor fd.
 * passes fd along with the appropriate arguments to a function to compile that
 * particular instruction */
static bool bfCompileInstruction(char c, int fd) {
    bool ret;
    instr = c;
    instr_col++;
    switch(c) {
      case '<':
        /* decrement the tape pointer register */
        ret = eamAsmDecReg(REG_BF_PTR, fd, &out_sz);
        if (!ret) appendError(instr, "Failed to write to file", "FAILED_WRITE");
        break;
      case '>':
        /* increment the tape pointer register */
        ret = eamAsmIncReg(REG_BF_PTR, fd, &out_sz);
        if (!ret) appendError(instr, "Failed to write to file", "FAILED_WRITE");
        break;
      case '+':
        /* increment the current tape value */
        ret = eamAsmIncByte(REG_BF_PTR, fd, &out_sz);
        if (!ret) appendError(instr, "Failed to write to file", "FAILED_WRITE");
        break;
      case '-':
        /* decrement the current tape value */
        ret = eamAsmDecByte(REG_BF_PTR, fd, &out_sz);
        if (!ret) appendError(instr, "Failed to write to file", "FAILED_WRITE");
        break;
      case '.':
        /* write to stdout */
        ret = bfIO(fd, STDOUT_FILENO, SYSCALL_WRITE);
        if (!ret) appendError(instr, "Failed to write to file", "FAILED_WRITE");
        break;
      case ',':
        /* read from stdin */
        ret = bfIO(fd, STDIN_FILENO, SYSCALL_READ);
        if (!ret) appendError(instr, "Failed to write to file", "FAILED_WRITE");
        break;
      case '[':
        ret = bfJumpOpen(fd);
        /* `[` and `]` do their own error handling. */
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
static bool bfExit(int fd) {
    bool ret = true;
    /* set system call register to exit system call numbifer */
    if (!eamAsmSetReg(REG_SC_NUM, SYSCALL_EXIT, fd, &out_sz)) {
        appendError(instr, "Failed to write exit syscall #", "FAILED_WRITE");
        ret = false;
    }
    /* set system call register to the desired exit code (0) */
    if (!eamAsmSetReg(REG_ARG1, 0, fd, &out_sz)) {
        appendError(instr, "Failed to write exit syscall arg1", "FAILED_WRITE");
        ret = false;
    }
    /* perform a system call */
    if (!eamAsmSyscall(fd, &out_sz)) {
        appendError(instr, "Failed to write syscall", "FAILED_WRITE");
        ret = false;
    }

    return ret;
}

static inline bool irCompileComplexInstruction(char *p, int fd, int* skip_p) {
    uint64_t ct;
    if (sscanf(p + 1, "%" SCNx64 "%n", &ct, skip_p) != 1) {
        appendError(instr, "Failed to read count for opcode.", "FAILED_SCAN");
        return false;
    } else {
        switch (*p) {
          case '#':
            return eamAsmAddMem(REG_BF_PTR, ct, fd, &out_sz);
          case '=':
            return eamAsmSubMem(REG_BF_PTR, ct, fd, &out_sz);
          case '}':
            return eamAsmAddRegByte(REG_BF_PTR, ct, fd, &out_sz);
          case '{':
            return eamAsmSubRegByte(REG_BF_PTR, ct, fd, &out_sz);
          case ')':
            return eamAsmAddRegWord(REG_BF_PTR, ct, fd, &out_sz);
          case '(':
            return eamAsmSubRegWord(REG_BF_PTR, ct, fd, &out_sz);
          case '$':
            return eamAsmAddRegDoubWord(REG_BF_PTR, ct, fd, &out_sz);
          case '^':
            return eamAsmSubRegDoubWord(REG_BF_PTR, ct, fd, &out_sz);
          case 'n':
            return eamAsmAddRegQuadWord(REG_BF_PTR, ct, fd, &out_sz);
          case 'N':
            return eamAsmSubRegQuadWord(REG_BF_PTR, ct, fd, &out_sz);
          default:
            appendError(instr, "Invalid IR Opcode", "INVALID_IR");
            return false;
        }
    }
}

static bool irCompileInstruction(char *p, int fd, int* skip_ct_p) {
    *skip_ct_p = 0;
    switch(*p) {
      case '+':
      case '-':
      case '<':
      case '>':
      case '.':
      case ',':
      case '[':
      case ']':
        return bfCompileInstruction(*p, fd);
      case '@':
        return eamAsmSetMemZero(REG_BF_PTR, fd, &out_sz);
      default:
        return irCompileComplexInstruction(p, fd, skip_ct_p);
    }
}

static bool irCompile(char *ir, int fd) {
    bool ret = true;
    char *p = ir;
    int skip_ct;
    while (*p) {
        instr_col++;
        instr = *p;
        ret &= irCompileInstruction(p++, fd, &skip_ct);
        p += skip_ct;
    }
    free(ir);
    return ret;
}

static bool bfCleanup(int fd) {
    bool ret = bfExit(fd);
    /* Ehdr and Phdr table are at the start */
    lseek(fd, 0, SEEK_SET);
    /* a |= b means a = (a | b) */
    ret |= writeEhdr(fd);
    ret |= writePhdrTable(fd);
    return ret;
}

/* maximum number of bytes to transfer from tmpfile at a time */
#define MAX_TRANS_SZ 4096
/* Takes 2 open file descriptors - in_fd and out_fd, and a boolean - optimize
 * in_fd is a brainfuck source file, open for reading.
 * out_fd is the destination file, open for writing.
 * if optimize is true, then first optimize code, then compile optimized code.
 *      (optimize is currently not used)
 *
 * It compiles the source code in in_fd, writing the output to out_fd.
 *
 * It does not verify that in_fd and out_fd are valid file descriptors,
 * nor that they are open properly.
 *
 * It calls several other functions to compile the source code. If any of
 * them return false it returns false as well.
 *
 * If all of the other functions succeeded, it returns true. */
bool bfCompile(int in_fd, int out_fd, bool optimize) {
    int ret = true;
    FILE *tmp_file = tmpfile();
    instr = '?';
    if (tmp_file == NULL) {
        appendError(instr, "Could not open a tmpfile.", "FAILED_TMPFILE");
        return false;
    }
    int tmp_fd = fileno(tmp_file);
    if (tmp_fd == -1) {
        appendError(
            instr,
            "Could not get file descriptor for tmpfile",
            "FAILED_TMPFILE"
        );
        fclose(tmp_file);
        return false;
    }
    /* reset out_sz variable used in several macros in eam_compiler_macros */
    out_sz = 0;
    /* reset the jump stack for the new file */
    JumpStack.index = 0;
    /* reset the error stack for the new file */
    resetErrors();
    /* reset the current line and column */
    instr_line = 1;
    instr_col = 0;

    /* skip the headers until we know the code size */
    if (fseek(tmp_file, START_PADDR, SEEK_SET) != 0) {
        appendError(instr, "Failed to seek to start of code.", "FAILED_SEEK");
        fclose(tmp_file);
        return false;
    }

    if (!eamAsmSetReg(REG_BF_PTR, TAPE_ADDRESS, tmp_fd, &out_sz)) {
        appendError(
            instr,
            "Failed to write initial setup instructions.",
            "FAILED_WRITE"
        );
        ret = false;
    }
    if (optimize) {
        char *ir = toIR(in_fd);
        if (ir == NULL) {
            fclose(tmp_file);
            return false;
        }
        ret &= irCompile(ir, tmp_fd);
    } else {
        /* the error message(s) are already appended if issues occur */
        while (read(in_fd, &instr, 1)) {
            ret &= bfCompileInstruction(instr, tmp_fd);
        }
    }

    /* now, code size is known, so we can write the headers
     * the appropriate error message(s) are already appended */
    if (!bfCleanup(tmp_fd)) ret = false;
    if(JumpStack.index > 0) {
        appendError(
            instr,
            "Reached the end of the file with an unmatched `[`!",
            "UNMATCHED_OPEN"
        );
        ret = false;
    }

    if (fseek(tmp_file, 0, SEEK_SET) != 0) {
        appendError(instr, "Failed to seek to start of tmpfile", "FAILED_SEEK");
        ret = false;
    }

    /* copy tmpfile over to the output file. */

    char trans[MAX_TRANS_SZ];
    ssize_t trans_sz;

    while ((trans_sz = read(tmp_fd, &trans, MAX_TRANS_SZ))) {
        if (trans_sz == -1) {
            appendError(
                instr,
                "Failed to read bytes from tmpfile",
                "FAILED_TMPFILE"
            );
            ret = false;
        } else if ((write(out_fd, &trans, trans_sz) != trans_sz)) {
            appendError(
                instr,
                "Failed to write bytes from tmpfile",
                "FAILED_TMPFILE"
            );
            ret = false;
        }
    }

    fclose(tmp_file);

    return ret;
}
