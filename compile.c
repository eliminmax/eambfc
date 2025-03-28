/* SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file contains the implementation of the actual compilation process.
 * It is by far the most significant part of the EAMBFC codebase. */

/* C99 */
#include <stdio.h>
#include <string.h>
/* POSIX */
#include <unistd.h>
/* internal */
#include "arch_inter.h"
#include "compat/elf.h"
#include "err.h"
#include "optimize.h"
#include "serialize.h"
#include "types.h"
#include "util.h"

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
#define LOAD_VADDR(tb) (((TAPE_ADDRESS + TAPE_SIZE(tb)) & (~0xffff)) + 0x10000)

/* physical address of the starting instruction
 * use the same technique as LOAD_VADDR to ensure that it is at a 256-byte
 * boundary. */
#define START_PADDR (((EHDR_SIZE + PHTB_SIZE) & ~0xff) + 0x100)

/* number of padding bytes between the end of the Program Header Table and the
 * beginning of the machine code. */
#define PAD_SZ (START_PADDR - (PHTB_SIZE + EHDR_SIZE))

static uint line, col;

/* Write the ELF header to the file descriptor fd. */
static bool write_ehdr(
    int fd, u64 tape_blocks, const arch_inter *inter, const char *out_name
) {
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
    header.e_ident[EI_DATA] = inter->elf_data;

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
    header.e_machine = inter->elf_arch;
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
    header.e_entry = LOAD_VADDR(tape_blocks) + START_PADDR;

    /* e_flags has a processor-specific meaning. For x86_64, no values are
     * defined, and it should be set to 0. */
    header.e_flags = inter->flags;

    if (inter->elf_data == ELFDATA2LSB) {
        serialize_ehdr64_le(&header, header_bytes);
    } else {
        serialize_ehdr64_be(&header, header_bytes);
    }

    return write_obj(fd, header_bytes, EHDR_SIZE, out_name);
}

/* Write the Program Header Table to the file descriptor fd
 * This is a list of areas within memory to set up when starting the program. */
static bool write_phtb(
    int fd,
    size_t code_sz,
    u64 tape_blocks,
    const arch_inter *inter,
    const char *out_name
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
    phdr_table[1].p_vaddr = LOAD_VADDR(tape_blocks);
    /* Load from this physical address */
    phdr_table[1].p_paddr = 0;
    /* Size within the file on disk - the size of the whole file, as this
     * segment contains the whole thing. */
    phdr_table[1].p_filesz = START_PADDR + code_sz;
    /* size within memory - must be at least p_filesz.
     * In this case, it's the size of the whole file, as the whole file is
     * loaded into this segment */
    phdr_table[1].p_memsz = START_PADDR + code_sz;
    /* supposed to be a power of 2, went with 2^0 */
    phdr_table[1].p_align = 1;

    for (int i = 0; i < PHNUM; i++) {
        if (inter->elf_data == ELFDATA2LSB) {
            serialize_phdr64_le(
                &(phdr_table[i]), &(phdr_table_bytes[i * PHDR_SIZE])
            );
        } else {
            serialize_phdr64_be(
                &(phdr_table[i]), &(phdr_table_bytes[i * PHDR_SIZE])
            );
        }
    }

    return write_obj(fd, phdr_table_bytes, PHTB_SIZE, out_name);
}

/* The brainfuck instructions "." and "," are similar from an implementation
 * perspective. Both require making system calls for I/O, and the system calls
 * have 3 nearly identical arguments:
 *  - arg1 is the file descriptor
 *  - arg2 is the memory address of the data source (write)/dest (read)
 *  - arg3 is the number of bytes to write/read
 *
 * Due to their similarity, ',' and '.' are both implemented with bf_io. */
static void bf_io(
    sized_buf *obj_code, int bf_fd, int sc, const arch_inter *inter
) {
    /* bf_fd is the brainfuck File Descriptor, not to be confused with fd,
     * the file descriptor of the output file.
     * sc is the system call number for the system call to use */
    /* load the number for the write system call into sc_num */
    inter->set_reg(inter->reg_sc_num, sc, obj_code);
    /* load the number for the stdout file descriptor into arg1 */
    inter->set_reg(inter->reg_arg1, bf_fd, obj_code);
    /* copy the address in bf_ptr to arg2 */
    inter->reg_copy(inter->reg_arg2, inter->reg_bf_ptr, obj_code);
    /* load # of bytes to read/write (1, specifically) into arg3 */
    inter->set_reg(inter->reg_arg3, 1, obj_code);
    /* finally, call the syscall instruction */
    inter->syscall(obj_code);
}

/* number of indexes in the jump stack to allocate for at a time */
#define JUMP_CHUNK_SZ 64

typedef struct jump_loc {
    uint src_line; /* saved for error reporting. */
    uint src_col; /* saved for error reporting. */
    off_t dst_loc;
} jump_loc;

static struct jump_stack {
    size_t next_index;
    size_t loc_sz;
    jump_loc *locations;
} jump_stack;

/* prepare to compile the brainfuck `[` instruction to file descriptor fd.
 * doesn't actually write to the file yet, as the address of `]` is unknown.
 *
 * If too many nested loops are encountered, it exteds the jump stack. */
static bool bf_jump_open(
    sized_buf *obj_code, const arch_inter *inter, const char *in_name
) {
    /* ensure that there are no more than the maximum nesting level */
    if (jump_stack.next_index + 1 == jump_stack.loc_sz) {
        if (jump_stack.loc_sz < SIZE_MAX - JUMP_CHUNK_SZ) {
            jump_stack.loc_sz += JUMP_CHUNK_SZ;
        } else {
            display_err((bf_comp_err){
                .id = BF_ERR_NESTED_TOO_DEEP,
                .msg = "Extending jump stack any more would cause an overflow",
                .file = in_name,
                .instr = '[',
                .has_location = false,
                .has_instr = true,
            });
            return false;
        }

        jump_stack.locations = checked_realloc(
            jump_stack.locations,
            (jump_stack.next_index + 1 + JUMP_CHUNK_SZ) * sizeof(jump_loc)
        );
    }
    /* push the current address onto the stack */
    jump_stack.locations[jump_stack.next_index].src_line = line;
    jump_stack.locations[jump_stack.next_index].src_col = col;
    jump_stack.locations[jump_stack.next_index].dst_loc = obj_code->sz;
    jump_stack.next_index++;
    /* insert an architecture-specific illegal/trap instruction, then pad to
     * proper size with no-op instructions so that the space for the jump open
     * is reserved before the address is known. */
    inter->pad_loop_open(obj_code);
    return true;
}

/* compile matching `[` and `]` instructions
 * called when `]` is the instruction to be compiled */
static bool bf_jump_close(sized_buf *obj_code, const arch_inter *inter) {
    off_t open_addr;
    i32 distance;

    /* ensure that the current index is in bounds */
    if (jump_stack.next_index == 0) {
        display_err((bf_comp_err){
            .id = BF_ERR_UNMATCHED_CLOSE,
            .file = NULL,
            .msg = "Found ']' without matching '['.",
            .has_instr = true,
            .has_location = true,
            .instr = ']',
            .line = line,
            .col = col,
        });
        return false;
    }
    /* pop the matching `[` instruction's location */
    open_addr = jump_stack.locations[--jump_stack.next_index].dst_loc;
    distance = obj_code->sz - open_addr;

    /* This is messy, but cuts down the number of allocations massively.
     * Because the NOP padding added earlier is the same size as the jump point,
     * if it made it this far, then enough space is allocated. By reporting the
     * allocated size accurately, but underreporting the used size as being the
     * index of the open address, the NOP padding will be replaced with the
     * conditional jump instruction without any risk of unnecessary reallocation
     * or any temporary buffer, sized or not. */

    sized_buf tmp_buf = {
        .buf = obj_code->buf, .sz = open_addr, .capacity = obj_code->capacity
    };

    if (!inter->jump_open(inter->reg_bf_ptr, distance, &tmp_buf)) {
        return false;
    }

    /* jumps to right after the `[` instruction, to skip a redundant check */
    return inter->jump_close(inter->reg_bf_ptr, -distance, obj_code);
}

/* 4 of the 8 brainfuck instructions can be compiled with instructions that take
 * the same set of parameters, so this expands to a call to the appropriate
 * function. */
#define COMPILE_WITH(f) f(inter->reg_bf_ptr, obj_code)

/* compile an individual instruction (c), to the file descriptor fd.
 * passes fd along with the appropriate arguments to a function to compile that
 * particular instruction */
static bool comp_instr(
    char c, sized_buf *obj_code, const arch_inter *inter, const char *in_name
) {
    col++;
    switch (c) {
        /* start with the simple cases handled with COMPILE_WITH */
        /* decrement the tape pointer register */
        case '<':
            COMPILE_WITH(inter->dec_reg);
            return true;
        /* increment the tape pointer register */
        case '>':
            COMPILE_WITH(inter->inc_reg);
            return true;
        /* increment the current tape value */
        case '+':
            COMPILE_WITH(inter->inc_byte);
            return true;
        /* decrement the current tape value */
        case '-':
            COMPILE_WITH(inter->dec_byte);
            return true;
        /* write to stdout */
        case '.':
            bf_io(obj_code, STDOUT_FILENO, inter->sc_write, inter);
            return true;
        /* read from stdin */
        case ',':
            bf_io(obj_code, STDIN_FILENO, inter->sc_read, inter);
            return true;
        /* `[` and `]` do their own error handling. */
        case '[':
            return bf_jump_open(obj_code, inter, in_name);
        case ']':
            return bf_jump_close(obj_code, inter);
        /* on a newline, add 1 to the line number and reset the column */
        case '\n':
            line++;
            col = 0;
            return true;
        /* any other characters are comments, so silently continue. */
        default:
            return true;
    }
}

/* similar to the above COMPILE_WITH, but with an extra parameter passed to the
 * function, so that can't be reused. */
#define IR_COMPILE_WITH(f) f(inter->reg_bf_ptr, count, obj_code)

/* Compile an ir instruction */
static bool comp_ir_instr(
    char instr,
    size_t count,
    sized_buf *obj_code,
    const arch_inter *inter,
    const char *in_name
) {
    /* if there's only one (non-'@') instruction, compile it normally */
    if (count == 1 && instr != '@')
        return comp_instr(instr, obj_code, inter, in_name);
    switch (instr) {
        /* if it's an unmodified brainfuck instruction, pass it to comp_instr */
        case '.':
        case ',':
        case '[':
        case ']':
            for (size_t i = 0; i < count; i++) {
                if (!comp_instr(instr, obj_code, inter, in_name)) return false;
            }
            return true;
        /* if it's @, then zero the byte pointed to by bf_ptr */
        case '@':
            inter->zero_byte(inter->reg_bf_ptr, obj_code);
            return true;
        case '+':
            IR_COMPILE_WITH(inter->add_byte);
            return true;
        case '-':
            IR_COMPILE_WITH(inter->sub_byte);
            return true;
        case '>':
            IR_COMPILE_WITH(inter->add_reg);
            return true;
        case '<':
            IR_COMPILE_WITH(inter->sub_reg);
            return true;
        default:
            internal_err(BF_ICE_INVALID_IR, "Invalid IR Opcode");
            return false;
    }
}

static bool compile_condensed(
    const char *src_code,
    sized_buf *obj_code,
    const arch_inter *inter,
    const char *in_name
) {
    /* return early when there are no instructions to compile */
    if (*src_code == '\0') return true;
    bool ret = true;
    size_t count = 1;
    char prev_instr = *(src_code);

    while (*(++src_code)) {
        if (*src_code == prev_instr) {
            count++;
        } else {
            ret &= comp_ir_instr(prev_instr, count, obj_code, inter, in_name);
            count = 1;
            prev_instr = *(src_code);
        }
    }
    return ret & comp_ir_instr(prev_instr, count, obj_code, inter, in_name);
}

/* Compile code in source file to destination file.
 * Parameters:
 * - inter is a pointer to the arch_inter backend used to provide the functions
 *   that compile brainfuck and EAMBFC-IR into machine code.
 * - in_fd is a brainfuck source file, open for reading.
 * - out_fd is the destination file, open for writing.
 * - optimize is a boolean indicating whether to optimize code before compiling.
 * - tape_blocks is the number of 4-KiB blocks to allocate for the tape.
 *
 * Returns true if compilation was successful, and false otherwise. */
bool bf_compile(
    const arch_inter *inter,
    const char *in_name,
    const char *out_name,
    int in_fd,
    int out_fd,
    bool optimize,
    u64 tape_blocks
) {
    sized_buf src_code = read_to_sized_buf(in_fd, in_name);
    /* Return immediately if a read failed */
    if (src_code.buf == NULL) return false;
    sized_buf obj_code = newbuf(BFC_CHUNK_SIZE);

    bool ret = true;

    /* reset the jump stack for the new file */
    jump_stack.next_index = 0;
    jump_stack.locations = checked_malloc(JUMP_CHUNK_SZ * sizeof(jump_loc));
    jump_stack.loc_sz = JUMP_CHUNK_SZ;

    /* reset the current line and column */
    line = 1;
    col = 0;

    /* set the bf_ptr register to the address of the start of the tape */
    inter->set_reg(inter->reg_bf_ptr, TAPE_ADDRESS, &obj_code);

    /* compile the actual source code to object code */
    if (optimize) {
        if (!filter_dead(&src_code, in_name)) {
            free(jump_stack.locations);
            return false;
        }
        ret &= compile_condensed(src_code.buf, &obj_code, inter, in_name);
    } else {
        for (size_t i = 0; i < src_code.sz; i++) {
            ret &= comp_instr(src_code.buf[i], &obj_code, inter, in_name);
        }
    }

    /* write code to perform the exit(0) syscall */
    /* set system call register to exit system call number */
    inter->set_reg(inter->reg_sc_num, inter->sc_exit, &obj_code);
    /* set system call register to the desired exit code (0) */
    inter->set_reg(inter->reg_arg1, 0, &obj_code);
    /* perform a system call */
    inter->syscall(&obj_code);

    /* now, obj_code size is known, so we can write the headers and padding */
    ret &= write_ehdr(out_fd, tape_blocks, inter, out_name);
    ret &= write_phtb(out_fd, obj_code.sz, tape_blocks, inter, out_name);
    const char padding[PAD_SZ] = {0};
    ret &= write_obj(out_fd, padding, PAD_SZ, out_name);
    /* finally, write the code itself. */
    ret &= write_obj(out_fd, obj_code.buf, obj_code.sz, out_name);

    /* check if any unmatched loop openings were left over. */
    for (size_t i = 0; i < jump_stack.next_index; i++) {
        display_err((bf_comp_err){
            .id = BF_ERR_UNMATCHED_OPEN,
            .file = in_name,
            .msg = "Reached the end of the file with an unmatched '['.",
            .instr = '[',
            .line = jump_stack.locations[i].src_line,
            .col = jump_stack.locations[i].src_col,
            .has_instr = true,
            .has_location = true,
        });
        ret = false;
    }

    free(obj_code.buf);
    free(src_code.buf);
    free(jump_stack.locations);

    return ret;
}
