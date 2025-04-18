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

    bf_comp_err e;
    if (write_obj(fd, header_bytes, EHDR_SIZE, &e)) return true;
    e.file = out_name;
    display_err(e);
    return false;
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
    bf_comp_err e;
    if (write_obj(fd, phdr_table_bytes, PHTB_SIZE, &e)) return true;
    e.file = out_name;
    display_err(e);
    return false;
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
    src_loc location;
    off_t dst_loc;
    bool has_loc: 1;
} jump_loc;

static struct jump_stack {
    size_t next;
    size_t loc_sz;
    jump_loc *locations;
} jump_stack;

/* prepare to compile the brainfuck `[` instruction to file descriptor fd.
 * doesn't actually write to the file yet, as the address of `]` is unknown.
 *
 * If too many nested loops are encountered, it exteds the jump stack. */
static bool bf_jump_open(
    sized_buf *restrict obj_code,
    const arch_inter *restrict inter,
    bf_comp_err *restrict err,
    const src_loc *loc
) {
    size_t start_loc = obj_code->sz;
    /* insert an architecture-specific illegal/trap instruction, then pad to
     * proper size with no-op instructions so that the space for the jump open
     * is reserved before the address is known. */
    inter->pad_loop_open(obj_code);
    /* ensure that there are no more than the maximum nesting level */
    if (jump_stack.next + 1 == jump_stack.loc_sz) {
        if (jump_stack.loc_sz < SIZE_MAX - JUMP_CHUNK_SZ) {
            jump_stack.loc_sz += JUMP_CHUNK_SZ;
        } else {
            *err = (bf_comp_err){
                .id = BF_ERR_NESTED_TOO_DEEP,
                .msg.ref =
                    "Extending jump stack any more would cause an overflow",
                .instr = '[',
                .has_instr = true,
            };
            return false;
        }

        jump_stack.locations = checked_realloc(
            jump_stack.locations,
            (jump_stack.next + 1 + JUMP_CHUNK_SZ) * sizeof(jump_loc)
        );
    }
    /* push the current address onto the stack */
    jump_stack.locations[jump_stack.next].dst_loc = start_loc;
    if (loc) {
        jump_stack.locations[jump_stack.next].location = *loc;
        jump_stack.locations[jump_stack.next].has_loc = true;
    } else {
        jump_stack.locations[jump_stack.next].location = (src_loc){0};
        jump_stack.locations[jump_stack.next].has_loc = false;
    }
    jump_stack.next++;
    return true;
}

/* compile matching `[` and `]` instructions
 * called when `]` is the instruction to be compiled */
static bool bf_jump_close(
    sized_buf *restrict obj_code,
    const arch_inter *restrict inter,
    bf_comp_err *restrict e
) {
    /* ensure that the current index is in bounds */
    if (jump_stack.next == 0) {
        *e = (bf_comp_err){
            .id = BF_ERR_UNMATCHED_CLOSE,
            .msg.ref = "Found ']' without matching '['.",
            .instr = ']',
            .has_instr = true,
        };
        return false;
    }
    /* pop the matching `[` instruction's location */
    off_t before = jump_stack.locations[--jump_stack.next].dst_loc;
    i32 distance = obj_code->sz - before;

    /* This is messy, but cuts down the number of allocations massively.
     * Because the NOP padding added earlier is the same size as the jump point,
     * if it made it this far, then enough space is allocated. By reporting the
     * allocated size accurately, but underreporting the used size as being the
     * index of the open address, the NOP padding will be replaced with the
     * conditional jump instruction without any risk of unnecessary reallocation
     * or any temporary buffer, sized or not. */

    if (!inter->jump_open(inter->reg_bf_ptr, distance, obj_code, before, e)) {
        return false;
    }

    /* jumps to right after the `[` instruction, to skip a redundant check */
    return inter->jump_close(inter->reg_bf_ptr, -distance, obj_code, e);
}

/* 4 of the 8 brainfuck instructions can be compiled with instructions that take
 * the same set of parameters, so this expands to a call to the appropriate
 * function. */
#define COMPILE_WITH(f) f(inter->reg_bf_ptr, obj_code)

/* compile an individual instruction (c), to the file descriptor fd.
 * passes fd along with the appropriate arguments to a function to compile that
 * particular instruction */
static bool comp_instr(
    char c,
    sized_buf *obj_code,
    const arch_inter *inter,
    const char *in_name,
    src_loc *restrict location
) {
    if (location) {
        /* if it's not a UTF-8 continuation byte, increment the column */
        if (((uchar)c & 0xc0) != 0x80) location->col++;
    }
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
        /* `[` and `]` could error out. */
        case '[': {
            bf_comp_err err;
            if (bf_jump_open(obj_code, inter, &err, location)) return true;
            err.file = in_name;
            if (location) {
                err.location = *location;
                err.has_location = true;
            }
            display_err(err);
            return false;
        }
        case ']': {
            bf_comp_err err;
            if (bf_jump_close(obj_code, inter, &err)) return true;
            err.file = in_name;
            if (location) {
                err.location = *location;
                err.has_location = true;
            }
            display_err(err);
            return false;
        }
        /* on a newline, add 1 to the line number and reset the column */
        case '\n':
            if (location) {
                location->line++;
                location->col = 0;
            }
            return true;
        /* any other characters are to be ignored, so silently continue. */
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
        return comp_instr(instr, obj_code, inter, in_name, NULL);
    switch (instr) {
        /* if it's an unmodified brainfuck instruction, pass it to comp_instr */
        case '.':
        case ',':
        case '[':
        case ']':
            for (size_t i = 0; i < count; i++) {
                if (!comp_instr(instr, obj_code, inter, in_name, NULL)) {
                    return false;
                }
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
    const char *restrict src_code,
    sized_buf *restrict obj_code,
    const arch_inter *restrict inter,
    const char *restrict in_name
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
    union read_result src;
    if (!read_to_sized_buf(in_fd, &src)) {
        src.err.file = in_name;
        display_err(src.err);
        return false;
    }
    sized_buf obj_code = newbuf(BFC_CHUNK_SIZE);

    bool ret = true;

    /* reset the jump stack for the new file */
    jump_stack.next = 0;
    jump_stack.locations = checked_malloc(JUMP_CHUNK_SZ * sizeof(jump_loc));
    jump_stack.loc_sz = JUMP_CHUNK_SZ;

    /* set the bf_ptr register to the address of the start of the tape */
    inter->set_reg(inter->reg_bf_ptr, TAPE_ADDRESS, &obj_code);

    /* compile the actual source code to object code */
    if (optimize) {
        if (!filter_dead(&src.sb, in_name)) {
            free(src.sb.buf);
            free(jump_stack.locations);
            free(obj_code.buf);
            return false;
        }
        ret &= compile_condensed(src.sb.buf, &obj_code, inter, in_name);
    } else {
        src_loc loc = {.line = 1, .col = 0};
        for (size_t i = 0; i < src.sb.sz; i++) {
            ret &= comp_instr(src.sb.buf[i], &obj_code, inter, in_name, &loc);
        }
    }

    /* write code to perform the exit(0) syscall */
    /* set system call register to exit system call number */
    inter->set_reg(inter->reg_sc_num, inter->sc_exit, &obj_code);
    /* set system call register to the desired exit code (0) */
    inter->set_reg(inter->reg_arg1, 0, &obj_code);
    /* perform a system call */
    inter->syscall(&obj_code);

    bf_comp_err e;

    /* now, obj_code size is known, so we can write the headers and padding */
    ret &= write_ehdr(out_fd, tape_blocks, inter, out_name);
    ret &= write_phtb(out_fd, obj_code.sz, tape_blocks, inter, out_name);
    const char padding[PAD_SZ] = {0};
    if (!write_obj(out_fd, padding, PAD_SZ, &e)) {
        e.file = out_name;
        display_err(e);
        ret = false;
    }
    /* finally, write the code itself. */
    if (!write_obj(out_fd, obj_code.buf, obj_code.sz, &e)) {
        e.file = out_name;
        display_err(e);
        ret = false;
    }

    /* check if any unmatched loop openings were left over. */
    for (size_t i = 0; i < jump_stack.next; i++) {
        display_err((bf_comp_err){
            .id = BF_ERR_UNMATCHED_OPEN,
            .file = in_name,
            .msg.ref = "Reached the end of the file with an unmatched '['.",
            .instr = '[',
            .has_instr = true,
            .location = jump_stack.locations[i].location,
            .has_location = jump_stack.locations[i].has_loc,
        });
        ret = false;
    }

    free(obj_code.buf);
    free(src.sb.buf);
    free(jump_stack.locations);

    return ret;
}

#ifdef BFC_TEST
/* POSIX */
#include <fcntl.h>
#include <sys/wait.h>
/* internal */
#include "unit_test.h"

/* spawn a child process, and pipe `src` in from a child process into
 * `bf_compile`. If an error occurs, it returns the bf_err_id left-shifted by 1,
 * with the lowest bit set to 1. Otherwise, it returns 0. */
static int test_compile(const char *src, bool optimize) {
    testing_err = TEST_SET;
    pid_t chld;
    ssize_t src_sz = strlen(src);
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) {
        perror("pipe");
        abort();
    }
    if ((chld = fork()) == -1) {
        perror("fork");
        abort();
    }
    if (chld == 0) {
        if (close(pipe_fds[0]) == -1) abort();
        const char *p = src;
        ssize_t write_sz;
        while (src_sz) {
            write_sz = write(pipe_fds[1], p, src_sz);
            if (write_sz == -1) abort();
            p -= write_sz;
            src_sz -= write_sz;
        }
        if (close(pipe_fds[1]) == -1) abort();
        exit(EXIT_SUCCESS);
    }
    if (close(pipe_fds[1]) == -1) {
        perror("close");
        abort();
    }

    testing_err = TEST_SET;
    int null_fd = open("/dev/null", O_WRONLY);
    if (null_fd == -1) {
        perror("open");
        abort();
    }

    bool comp_ret = bf_compile(
        &BFC_DEFAULT_INTER,
        "dummy.bf",
        "dummy",
        pipe_fds[0],
        null_fd,
        optimize,
        8
    );
    if (close(pipe_fds[0]) == -1 || close(null_fd) == -1) {
        perror("close");
        abort();
    }
    int chld_stat;
    if (waitpid(chld, &chld_stat, 0) == -1) {
        perror("waitpid");
        abort();
    }
    if (!WIFEXITED(chld_stat) || WEXITSTATUS(chld_stat) != EXIT_SUCCESS) {
        fputs("Child exited abnormally\n", stderr);
        abort();
    }

    return comp_ret ? 0 : (current_err << 1) | 1;
}

static void compile_all_bf_instructions(void) {
    CU_ASSERT(test_compile("+[>]<-,.", false) == 0);
}

static void compile_nested_loops(void) {
    CU_ASSERT(test_compile(">+[-->---[-<]>]>+", false) == 0);
}

static void unmatched_open(void) {
    CU_ASSERT(test_compile("[", false) == ((BF_ERR_UNMATCHED_OPEN << 1) | 1));
    CU_ASSERT(test_compile("[", true) == ((BF_ERR_UNMATCHED_OPEN << 1) | 1));
}

static void unmatched_close(void) {
    CU_ASSERT(test_compile("]", false) == ((BF_ERR_UNMATCHED_CLOSE << 1) | 1));
    CU_ASSERT(test_compile("]", true) == ((BF_ERR_UNMATCHED_CLOSE << 1) | 1));
}

CU_pSuite register_compile_tests(void) {
    CU_pSuite suite;
    INIT_SUITE(suite);
    ADD_TEST(suite, compile_all_bf_instructions);
    ADD_TEST(suite, compile_nested_loops);
    ADD_TEST(suite, unmatched_open);
    ADD_TEST(suite, unmatched_close);
    return suite;
}

#endif /* BFC_TEST */
