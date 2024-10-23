/* SPDX-FileCopyrightText: 2024 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains the implementation of the actual compilation process.
 * It is by far the most significant part of the EAMBFC codebase. */

/* C99 */
#include <stdlib.h> /* malloc, realloc, free */
#include <stdio.h> /* sscanf, fileno, tmpfile, fclose, fseek, FILE */
/* POSIX */
#include <unistd.h> /* off_t, read, write, seek. STD*_FILENO*/
/* internal */
#include "arch_inter.h" /* arch_registers, arch_sc_nums, arch_inter */
#include "compat/elf.h" /* Elf64_Ehdr, Elf64_Phdr, ELFDATA2[LM]SB */
#include "err.h" /* *_err */
#include "optimize.h" /* to_ir */
#include "serialize.h" /* serialize_*hdr64_[bl]e */
#include "types.h" /* bool, int*_t, uint*_t, SCNx64 */
#include "util.h" /* write_obj */

/* virtual memory address of the tape - cannot overlap with the machine code.
 * 0 is invalid as it's the null address, so this is an arbitrarily-chosen
 * starting point that's easy to reason about. */
#define TAPE_ADDRESS 0x10000

/* number of entries in program and section header tables respectively */
#define PHNUM 2 /* one for the tape, and one for the code. */

/* size of the Ehdr struct, once serialized. */
#define EHDR_SIZE 64

/* Sizes of a single PHDR table entry */
#define PHDR_SIZE 56
/* sizes of the full program header table */
#define PHTB_SIZE (PHNUM * PHDR_SIZE)

#define TAPE_SIZE(tb) (tb * 0x1000)
/* virtual address of the section containing the machine code
 * should be after the tape ends to avoid overlapping with the tape.
 *
 * Zero out the lowest 2 bytes of the end of the tape and add 0x10000 to ensure
 * that there is enough room. */
#define LOAD_VADDR(tb) (((TAPE_ADDRESS + TAPE_SIZE(tb)) & (~ 0xffff)) + 0x10000)

/* physical address of the starting instruction
 * use the same technique as LOAD_VADDR to ensure that it is at a 256-byte
 * boundary. */
#define START_PADDR (((((EHDR_SIZE + PHTB_SIZE)) & ~ 0xff) + 0x100))

/* the current size of the file
 * - if called mid-compilation, it will be the size as of the most
 *   recently-compiled machine code instruction.
 * - if called at the end, it will be the final file size. */
#define CURRENT_SIZE(sz) (START_PADDR + sz)

static off_t out_sz;
static uint _line;
static uint _col;

/* Write the ELF header to the file descriptor fd. */
static bool write_ehdr(int fd, const arch_inter *inter) {

    /* The format of the ELF header is well-defined and well-documented
     * elsewhere. The struct for it is defined in compat/elf.h, as are most
     * of the values used in here. */

    Elf64_Ehdr header;
    char header_bytes[EHDR_SIZE];

    /* the first 4 bytes are "magic values" that are pre-defined and used to
     * identify the format. */
    header.e_ident[EI_MAG0] = ELFMAG0;
    header.e_ident[EI_MAG1] = ELFMAG1;
    header.e_ident[EI_MAG2] = ELFMAG2;
    header.e_ident[EI_MAG3] = ELFMAG3;

    /* Target is a 64-bit architecture. */
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = inter->ELF_DATA;

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
    for (int i = EI_PAD; i < EI_NIDENT; i++) header.e_ident[i] = 0;

    /* this is a basic executable */
    header.e_type = ET_EXEC;

    /* TARGET_ARCH is defined in config.h, and values are also the values for
     * target ELF architectures. */
    header.e_machine = inter->ELF_ARCH;
    /* e_version, like e_ident[EI_VERSION], must be set to EV_CURRENT */
    header.e_version = EV_CURRENT;

    /* the remaining parts of the ELF header are defined in a different order
     * than their ordering within the struct, because I believe it's easier
     * to make sense of them in this order. */

    /* the number of program and section table entries, respectively */
    header.e_phnum = PHNUM;
    header.e_shnum = 0;

    /* The offset within the file for the program and section header tables
     * respectively. */
    header.e_phoff = EHDR_SIZE; /* start right after the EHDR ends */
    header.e_shoff = 0;

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
    header.e_entry = LOAD_VADDR(out_sz) + START_PADDR;

    /* e_flags has a processor-specific meaning. For x86_64, no values are
     * defined, and it should be set to 0. */
    header.e_flags = inter->FLAGS;

    if (inter->ELF_DATA == ELFDATA2LSB) {
        serialize_ehdr64_le(&header, header_bytes);
    } else {
        serialize_ehdr64_be(&header, header_bytes);
    }

    off_t dummy = 0;
    return write_obj(fd, header_bytes, EHDR_SIZE, &dummy);
}

/* Write the Program Header Table to the file descriptor fd
 * This is a list of areas within memory to set up when starting the program. */
static inline bool write_phtb(
    int fd, uint64_t tape_blocks, const arch_inter *inter
) {
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
    phdr_table[0].p_memsz = TAPE_SIZE(tape_blocks);
    /* supposed to be a power of 2, went with 2^12 */
    phdr_table[0].p_align = 0x1000;

    /* header for the segment that contains the actual binary */
    phdr_table[1].p_type = PT_LOAD;
    /* It is readable and executable */
    phdr_table[1].p_flags = PF_R | PF_X;
    /* Load initial bytes from this offset within the file */
    phdr_table[1].p_offset = 0;
    /* Start at this memory address */
    phdr_table[1].p_vaddr = LOAD_VADDR(out_sz);
    /* Load from this physical address */
    phdr_table[1].p_paddr = 0;
    /* Size within the file on disk - the size of the whole file, as this
     * segment contains the whole thing. */
    phdr_table[1].p_filesz = CURRENT_SIZE(out_sz);
    /* size within memory - must be at least p_filesz.
     * In this case, it's the size of the whole file, as the whole file is
     * loaded into this segment */
    phdr_table[1].p_memsz = CURRENT_SIZE(out_sz);
    /* supposed to be a power of 2, went with 2^0 */
    phdr_table[1].p_align = 1;

    for (int i = 0; i < PHNUM; i++) {
        if (inter->ELF_DATA == ELFDATA2LSB) {
            serialize_phdr64_le(
                &(phdr_table[i]),
                &(phdr_table_bytes[i * PHDR_SIZE])
            );
        } else {
            serialize_phdr64_be(
                &(phdr_table[i]),
                &(phdr_table_bytes[i * PHDR_SIZE])
            );
        }
    }

    off_t dummy = 0;
    return write_obj(fd, phdr_table_bytes, PHTB_SIZE, &dummy);
}

/* The brainfuck instructions "." and "," are similar from an implementation
 * perspective. Both require making system calls for I/O, and the system calls
 * have 3 nearly identical arguments:
 *  - arg1 is the file descriptor
 *  - arg2 is the memory address of the data source (write)/dest (read)
 *  - arg3 is the number of bytes to write/read
 *
 * Due to their similarity, ',' and '.' are both implemented with bf_io. */
static inline bool bf_io(int fd, int bf_fd, int sc, const arch_inter *inter) {
    /* bf_fd is the brainfuck File Descriptor, not to be confused with fd,
     * the file descriptor of the output file.
     * sc is the system call number for the system call to use */
    return (
        /* load the number for the write system call into sc_num */
        inter->FUNCS->set_reg(inter->REGS->sc_num, sc, fd, &out_sz) &&
        /* load the number for the stdout file descriptor into arg1 */
        inter->FUNCS->set_reg(inter->REGS->arg1, bf_fd, fd, &out_sz) &&
        /* copy the address in bf_ptr to arg2 */
        inter->FUNCS->reg_copy(
            inter->REGS->arg2, inter->REGS->bf_ptr, fd, &out_sz
        ) &&
        /* load # of bytes to read/write (1, specifically) into arg3 */
        inter->FUNCS->set_reg(inter->REGS->arg3, 1, fd, &out_sz) &&
        /* finally, call the syscall instruction */
        inter->FUNCS->syscall(fd, &out_sz)
    );
}

/* number of indexes in the jump stack to allocate for at a time */
#define JUMP_CHUNK_SZ 64

typedef struct jump_loc {
    uint src_line; /* saved for error reporting. */
    uint src_col; /* saved for error reporting. */
    off_t dst_loc;
} jump_loc;

static struct jump_stack {
    size_t index;
    size_t loc_sz;
    jump_loc *locations;
} jump_stack;

/* prepare to compile the brainfuck `[` instruction to file descriptor fd.
 * doesn't actually write to the file yet, as the address of `]` is unknown.
 *
 * If too many nested loops are encountered, tries to resize the jump stack.
 * If that fails, sets alloc_valve to false and aborts. */
static inline bool bf_jump_open(
    int fd, bool *alloc_valve, const arch_inter *inter
) {
    /* ensure that there are no more than the maximum nesting level */
    if (jump_stack.index + 1 == jump_stack.loc_sz) {
        if (jump_stack.loc_sz < SIZE_MAX - JUMP_CHUNK_SZ) {
            jump_stack.loc_sz += JUMP_CHUNK_SZ;
        } else {
            basic_err(
                "TOO_MANY_NESTED_LOOPS",
                "Extending jump stack any more would cause an overflow."
            );
            *alloc_valve = false;
        }

        jump_stack.locations = realloc(
            jump_stack.locations,
            (jump_stack.index + 1 + JUMP_CHUNK_SZ) * sizeof(jump_loc)
        );
        if (jump_stack.locations == NULL) {
            alloc_err();
            *alloc_valve = false;
            return false;
        }
    }
    /* push the current address onto the stack */
    jump_stack.locations[jump_stack.index].src_line = _line;
    jump_stack.locations[jump_stack.index].src_col = _col;
    jump_stack.locations[jump_stack.index].dst_loc = out_sz;
    jump_stack.index++;
    /* fill space jump open will take with NOP instructions of the same length,
     * so that out_sz remains properly sized. */
    return inter->FUNCS->nop_loop_open(fd, &out_sz);
}

/* compile matching `[` and `]` instructions
 * called when `]` is the instruction to be compiled */
static inline bool bf_jump_close(int fd, const arch_inter *inter) {
    off_t open_address, close_address;
    int32_t distance;

    /* ensure that the current index is in bounds */
    if (jump_stack.index == 0) {
        position_err(
            "UNMATCHED_CLOSE",
            "Found ']' without matching '['.",
            ']',
            _line,
            _col
        );
        return false;
    }
    /* pop the matching `[` instruction's location */
    open_address = jump_stack.locations[--jump_stack.index].dst_loc;
    close_address = out_sz;

    distance = close_address - open_address;

    /* jump to the skipped `[` instruction, write it, and jump back */
    if (lseek(fd, open_address, SEEK_SET) != open_address) {
        instr_err("FAILED_SEEK", "Failed to return to '[' instruction.", '[');
        return false;
    }
    off_t phony = 0; /* already added to code size for this one */
    if (!inter->FUNCS->jump_zero(inter->REGS->bf_ptr, distance, fd, &phony)) {
        return false;
    }

    if (lseek(fd, close_address, SEEK_SET) != close_address) {
        position_err(
            "FAILED_SEEK", "Failed to return to ']'.", ']', _line, _col
        );
        return false;
    }
    /* jumps to right after the `[` instruction, to skip a redundant check */
    return inter->FUNCS->jump_not_zero(
        inter->REGS->bf_ptr, -distance, fd, &out_sz
    );
}

/* 4 of the 8 brainfuck instructions can be compiled with instructions that take
 * the same set of parameters, so this expands to a call to the appropriate
 * function. */
#define COMPILE_WITH(f) f(inter->REGS->bf_ptr, fd, &out_sz)

/* compile an individual instruction (c), to the file descriptor fd.
 * passes fd along with the appropriate arguments to a function to compile that
 * particular instruction */
static bool comp_instr(
    char c,
    int fd,
    bool *alloc_valve,
    const arch_inter *inter
) {
    _col++;
    switch(c) {
      /* start with the simple cases handled with COMPILE_WITH */
      /* decrement the tape pointer register */
      case '<': return COMPILE_WITH(inter->FUNCS->dec_reg);
      /* increment the tape pointer register */
      case '>': return COMPILE_WITH(inter->FUNCS->inc_reg);
      /* increment the current tape value */
      case '+': return COMPILE_WITH(inter->FUNCS->inc_byte);
      /* decrement the current tape value */
      case '-': return COMPILE_WITH(inter->FUNCS->dec_byte);
      /* write to stdout */
      case '.': return bf_io(fd, STDOUT_FILENO, inter->SC_NUMS->write, inter);
      /* read from stdin */
      case ',': return bf_io(fd, STDIN_FILENO, inter->SC_NUMS->read, inter);
      /* `[` and `]` do their own error handling. */
      case '[': return bf_jump_open(fd, alloc_valve, inter);
      case ']': return bf_jump_close(fd, inter);
      case '\n':
        /* add 1 to the line number and reset the column. */
        _line++;
        _col = 0;
        return true;
      default:
        /* any other characters are comments, silently continue. */
        return true;
    }
}

/* similar to the above COMPILE_WITH, but with an extra parameter passed to the
 * function, so that can't be reused. */
#define IR_COMPILE_WITH(f) f(inter->REGS->bf_ptr, ct, fd, &out_sz)
/* compile a condensed instruction sequence */
static inline bool comp_ir_condensed_instr(
    char *p,
    int fd,
    int *skip_p,
    const arch_inter *inter
) {
    uint64_t ct;
    if (sscanf(p + 1, "%" SCNx64 "%n", &ct, skip_p) != 1) {
        basic_err("IR_FAILED_SCAN", "Failed to get count for EAMBFC-IR op.");
        return false;
    } else {
        switch (*p) {
          case '#': return IR_COMPILE_WITH(inter->FUNCS->add_byte);
          case '=': return IR_COMPILE_WITH(inter->FUNCS->sub_byte);
          case '}': return IR_COMPILE_WITH(inter->FUNCS->add_reg);
          case '{': return IR_COMPILE_WITH(inter->FUNCS->sub_reg);
          default:
            basic_err("INVALID_IR", "Invalid IR Opcode");
            return false;
        }
    }
}

static inline bool comp_ir_instr(
    char *p,
    int fd,
    int *skip_ct_p,
    bool *alloc_valve,
    const arch_inter *inter
) {
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
        return comp_instr(*p, fd, alloc_valve, inter);
      case '@':
        return inter->FUNCS->zero_byte(inter->REGS->bf_ptr, fd, &out_sz);
      default:
        return comp_ir_condensed_instr(p, fd, skip_ct_p, inter);
    }
}

static inline bool comp_ir(char *ir, int fd, const arch_inter *inter) {
    bool ret = true;
    char *p = ir;
    int skip_ct;
    bool alloc_valve = true;
    while (*p) {
        _col++;
        ret &= comp_ir_instr(p++, fd, &skip_ct, &alloc_valve, inter);
        if (!alloc_valve) return false;
        p += skip_ct;
    }
    free(ir);
    return ret;
}

static inline bool finalize(int fd, uint64_t tb, const arch_inter *inter) {
    /* write code to perform the exit(0) syscall */
    bool ret = (
        /* set system call register to exit system call number */
        inter->FUNCS->set_reg(
            inter->REGS->sc_num,
            inter->SC_NUMS->exit,
            fd,
            &out_sz
        ) &&
        /* set system call register to the desired exit code (0) */
        inter->FUNCS->set_reg(inter->REGS->arg1, 0, fd, &out_sz) &&
        /* perform a system call */
        inter->FUNCS->syscall(fd, &out_sz)
    );

    /* Ehdr and Phdr table are at the start */
    if (lseek(fd, 0, SEEK_SET) != 0) {
        basic_err("FAILED_SEEK", "Failed to seek to start of code.");
        return false;
    }

    return ret && write_ehdr(fd, inter) && write_phtb(fd, tb, inter);
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
bool bf_compile(
    const arch_inter *inter,
    int in_fd,
    int out_fd,
    bool optimize,
    uint64_t tape_blocks
) {
    int ret = true;
    FILE *tmp_file = tmpfile();
    if (tmp_file == NULL) {
        basic_err("FAILED_TMPFILE", "Could not open a tmpfile.");
        return false;
    }
    int tmp_fd = fileno(tmp_file);
    if (tmp_fd == -1) {
        basic_err(
            "FAILED_TMPFILE",
            "Could not get file descriptor for tmpfile"
        );
        fclose(tmp_file);
        return false;
    }
    /* reset out_sz variable used in several macros in compiler_macros */
    out_sz = 0;
    /* reset the jump stack for the new file */
    jump_stack.index = 0;
    jump_stack.locations = malloc(JUMP_CHUNK_SZ * sizeof(jump_loc));
    if (jump_stack.locations == NULL) {
        alloc_err();
        fclose(tmp_file);
        return false;
    }
    jump_stack.loc_sz = JUMP_CHUNK_SZ;
    /* reset the current line and column */
    _line = 1;
    _col = 0;
    char _instr = '\0';

    /* skip the headers until we know the code size */
    if (fseek(tmp_file, START_PADDR, SEEK_SET) != 0) {
        basic_err("FAILED_SEEK", "Failed to seek to start of code.");
        fclose(tmp_file);
        return false;
    }

    ret &= inter->FUNCS->set_reg(
        inter->REGS->bf_ptr,
        TAPE_ADDRESS,
        tmp_fd,
        &out_sz
    );

    if (optimize) {
        char *ir = to_ir(in_fd);
        if (ir == NULL) {
            fclose(tmp_file);
            return false;
        }
        ret &= comp_ir(ir, tmp_fd, inter);
    } else {
        /* the error message(s) are already appended if issues occur */
        while (read(in_fd, &_instr, 1)) {
            bool alloc_valve = true;
            ret &= comp_instr(_instr, tmp_fd, &alloc_valve, inter);
            if (!alloc_valve) {
                fclose(tmp_file);
                return false;
            }
        }
    }

    /* now, code size is known, so we can write the headers
     * the appropriate error message(s) are already appended */
    if (!finalize(tmp_fd, tape_blocks, inter)) ret = false;
    /* check if any unmatched loop openings were left over. */
    if (jump_stack.index-- > 0) {
        position_err(
            "UNMATCHED_OPEN",
            "Reached the end of the file with an unmatched '['.",
            '[',
            jump_stack.locations[jump_stack.index].src_line,
            jump_stack.locations[jump_stack.index].src_col
        );
        ret = false;
    }

    if (fseek(tmp_file, 0, SEEK_SET) != 0) {
        basic_err("FAILED_SEEK", "Failed to seek to start of tmpfile");
        ret = false;
    }

    /* copy tmpfile over to the output file. */
    char trans[MAX_TRANS_SZ];
    ssize_t trans_sz;

    while ((trans_sz = read(tmp_fd, &trans, MAX_TRANS_SZ))) {
        if (trans_sz == -1) {
            basic_err("FAILED_TMPFILE", "Failed to read from tmpfile");
            ret = false;
        } else if ((write(out_fd, &trans, trans_sz) != trans_sz)) {
            basic_err("FAILED_TMPFILE", FAILED_WRITE_MSG);
            ret = false;
        }
    }

    fclose(tmp_file);
    free(jump_stack.locations);

    return ret;
}
