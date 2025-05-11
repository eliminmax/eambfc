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
#include <types.h>

#include "arch_inter.h"
#include "compile.h"
#include "err.h"
#include "optimize.h"
#include "serialize.h"
#include "util.h"

/* virtual memory address of the tape - cannot overlap with the machine code.
 * 0 is invalid as it's the null address, so this is an arbitrarily-chosen
 * starting point that's easy to reason about. */
#define TAPE_ADDRESS 0x10000

#define TAPE_SIZE(tb) (tb * 0x1000)

/* virtual address of the section containing the machine code
 * should be after the tape ends to avoid overlapping with the tape.
 *
 * Zero out the lowest 2 bytes of the end of the tape and add 0x10000 to ensure
 * that there is enough room. */
#define LOAD_VADDR(tb) (((TAPE_ADDRESS + TAPE_SIZE(tb)) & (~0xffff)) + 0x10000)

/* address in file of the starting instruction - because the headers are padded
 * to 256 bytes, this will always be 256. */
#define START_PADDR 256

/* byte sequence used to identify an ELF file */
#define MAGIC_BYTES 0x7f, 'E', 'L', 'F'

nonnull_args static bool write_headers(
    int fd,
    u64 tape_blocks,
    const ArchInter *restrict inter,
    size_t code_sz,
    const char *restrict out_name
) {
    ElfInfo ehdr = {
        .e_ident =
            {/* The ELF identifying magic bytes */
             MAGIC_BYTES,
             /* address size marker */
             inter->addr_size,
             /* endianness marker */
             inter->elf_data,
             /* current ELF version (only valid value) */
             1,
             /* SYSV ABI */
             0,
             /* unspecified ABI version, as none are defined for SYSV */
             0
            },
        .e_entry = LOAD_VADDR(tape_blocks) + START_PADDR,
        .e_flags = inter->flags,
        .e_machine = inter->elf_arch,
        .e_phnum = 2,
    };
    SegmentInfo phdr_table[2];
    phdr_table[0].addr_size = phdr_table[1].addr_size = inter->addr_size;

    phdr_table[0].p_flags = SEG_R | SEG_W;
    phdr_table[0].p_align = 0x1000;
    phdr_table[0].file_backed = false;
    phdr_table[0].size = TAPE_SIZE(tape_blocks);
    phdr_table[0].virtaddr = TAPE_ADDRESS;

    phdr_table[1].p_flags = SEG_R | SEG_X;
    phdr_table[1].p_align = 1;
    phdr_table[1].file_backed = true;
    phdr_table[1].size = START_PADDR + code_sz;
    phdr_table[1].virtaddr = LOAD_VADDR(tape_blocks);

    char header_bytes[256];
    size_t i = 0;
    if (inter->elf_data == BYTEORDER_LSB) {
        i += serialize_ehdr_le(&ehdr, header_bytes);
        i += serialize_phdr_le(&phdr_table[0], &header_bytes[i]);
        i += serialize_phdr_le(&phdr_table[1], &header_bytes[i]);
    } else {
        i += serialize_ehdr_be(&ehdr, header_bytes);
        i += serialize_phdr_be(&phdr_table[0], &header_bytes[i]);
        i += serialize_phdr_be(&phdr_table[1], &header_bytes[i]);
    }
    memset(&header_bytes[i], 0, 256 - i);
    BFCError e;
    if (write_obj(fd, header_bytes, 256, &e)) return true;
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
    SizedBuf *obj_code, int bf_fd, int sc, const ArchInter *inter
) {
    /* bf_fd is the brainfuck File Descriptor, not to be confused with fd,
     * the file descriptor of the output file.
     * sc is the system call number for the system call to use
     *
     * Both uses of `set_reg` are always going to succeed, as `bf_fd` always
     * fits within 32 bits */
    /* load the number for the stdout file descriptor into arg1 */
    inter->set_reg(inter->reg_arg1, bf_fd, obj_code, NULL);
    /* copy the address in bf_ptr to arg2 */
    inter->reg_copy(inter->reg_arg2, inter->reg_bf_ptr, obj_code);
    /* load # of bytes to read/write (1, specifically) into arg3 */
    inter->set_reg(inter->reg_arg3, 1, obj_code, NULL);
    /* finally, call the syscall instruction */
    inter->syscall(obj_code, sc);
}

/* number of indexes in the jump stack to allocate for at a time */
#define JUMP_CHUNK_SZ 64

typedef struct {
    SrcLoc location;
    off_t dst_loc;
    bool has_loc: 1;
} JumpLoc;

static struct {
    size_t next;
    size_t loc_sz;
    JumpLoc *locations;
} jump_stack;

/* prepare to compile the brainfuck `[` instruction to file descriptor fd.
 * doesn't actually write to the file yet, as the address of `]` is unknown.
 *
 * If too many nested loops are encountered, it exteds the jump stack. */
static bool bf_jump_open(
    SizedBuf *restrict obj_code,
    const ArchInter *restrict inter,
    BFCError *restrict err,
    const SrcLoc *loc
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
            *err = (BFCError){
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
            (jump_stack.next + 1 + JUMP_CHUNK_SZ) * sizeof(JumpLoc)
        );
    }
    /* push the current address onto the stack */
    jump_stack.locations[jump_stack.next].dst_loc = start_loc;
    if (loc) {
        jump_stack.locations[jump_stack.next].location = *loc;
        jump_stack.locations[jump_stack.next].has_loc = true;
    } else {
        jump_stack.locations[jump_stack.next].location = (SrcLoc){0};
        jump_stack.locations[jump_stack.next].has_loc = false;
    }
    jump_stack.next++;
    return true;
}

/* compile matching `[` and `]` instructions
 * called when `]` is the instruction to be compiled */
static bool bf_jump_close(
    SizedBuf *restrict obj_code,
    const ArchInter *restrict inter,
    BFCError *restrict e
) {
    /* ensure that the current index is in bounds */
    if (jump_stack.next == 0) {
        *e = (BFCError){
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
    SizedBuf *obj_code,
    const ArchInter *inter,
    const char *in_name,
    SrcLoc *restrict location
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
            BFCError err;
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
            BFCError err;
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

static bool compile_condensed(
    const InstrSeq *restrict instr_seqs,
    size_t len,
    SizedBuf *restrict obj_code,
    const ArchInter *restrict inter,
    const char *restrict in_name
) {
    bool ret = true;
    for (size_t i = 0; i < len; ++i) {
        switch (instr_seqs[i].tag) {
            case ISEQ_SET_CELL:
                inter->zero_byte(inter->reg_bf_ptr, obj_code);
                inter->add_byte(
                    inter->reg_bf_ptr, instr_seqs[i].count, obj_code
                );
                break;
            case ISEQ_ADD:
                inter->add_byte(
                    inter->reg_bf_ptr, instr_seqs[i].count, obj_code
                );
                break;
            case ISEQ_SUB:
                inter->sub_byte(
                    inter->reg_bf_ptr, instr_seqs[i].count, obj_code
                );
                break;
            case ISEQ_MOVE_RIGHT: {
                BFCError err;
                if (!inter->add_reg(
                        inter->reg_bf_ptr, instr_seqs[i].count, obj_code, &err
                    )) {
                    err.file = in_name;
                    err.has_location = true;
                    err.location = instr_seqs[i].source.location;
                    display_err(err);
                    ret = false;
                }
            } break;
            case ISEQ_MOVE_LEFT: {
                BFCError err;
                if (!inter->sub_reg(
                        inter->reg_bf_ptr, instr_seqs[i].count, obj_code, &err
                    )) {
                    err.file = in_name;
                    err.has_location = true;
                    err.location = instr_seqs[i].source.location;
                    display_err(err);
                    ret = false;
                }
            } break;
            case ISEQ_LOOP_OPEN: {
                BFCError err;
                if (!bf_jump_open(
                        obj_code, inter, &err, &instr_seqs[i].source.location
                    )) {
                    err.file = in_name;
                    err.has_location = true;
                    err.location = instr_seqs[i].source.location;
                    display_err(err);
                    ret = false;
                }
            } break;
            case ISEQ_LOOP_CLOSE: {
                BFCError err;
                if (!bf_jump_close(obj_code, inter, &err)) {
                    err.file = in_name;
                    err.has_location = true;
                    err.location = instr_seqs[i].source.location;
                    display_err(err);
                    ret = false;
                }
            } break;
            case ISEQ_WRITE:
                bf_io(obj_code, STDOUT_FILENO, inter->sc_write, inter);
                break;
            case ISEQ_READ:
                bf_io(obj_code, STDIN_FILENO, inter->sc_read, inter);
                break;
            default:
                break;
        }
    }
    return ret;
}

/* Compile code in source file to destination file.
 * Parameters:
 * - inter is a pointer to the ArchInter backend used to provide the functions
 *   that compile brainfuck and EAMBFC-IR into machine code.
 * - in_fd is a brainfuck source file, open for reading.
 * - out_fd is the destination file, open for writing.
 * - optimize is a boolean indicating whether to optimize code before compiling.
 * - tape_blocks is the number of 4-KiB blocks to allocate for the tape.
 *
 * Returns true if compilation was successful, and false otherwise. */
bool bf_compile(
    const ArchInter *inter,
    const char *in_name,
    const char *out_name,
    int in_fd,
    int out_fd,
    bool optimize,
    u64 tape_blocks
) {
    union read_result src;
    if (!read_to_sb(in_fd, &src)) {
        src.err.file = in_name;
        display_err(src.err);
        return false;
    }
    SizedBuf obj_code = newbuf(BFC_CHUNK_SIZE);

    bool ret = true;

    /* reset the jump stack for the new file */
    jump_stack.next = 0;
    jump_stack.locations = checked_malloc(JUMP_CHUNK_SZ * sizeof(JumpLoc));
    jump_stack.loc_sz = JUMP_CHUNK_SZ;

    /* set the bf_ptr register to the address of the start of the tape
     * start address is a constant that fits within 32-bit address space, so
     * this will always succeed. */
    inter->set_reg(inter->reg_bf_ptr, TAPE_ADDRESS, &obj_code, NULL);

    /* compile the actual source code to object code */
    if (optimize) {
        union opt_result res;
        if (!optimize_instructions(src.sb.buf, src.sb.sz, &res)) {
            res.err.file = in_name;
            display_err(res.err);
            free(src.sb.buf);
            free(jump_stack.locations);
            free(obj_code.buf);
            return false;
        }
        ret &= compile_condensed(
            res.output.instrs, res.output.len, &obj_code, inter, in_name
        );
        free(res.output.instrs);
    } else {
        SrcLoc loc = {.line = 1, .col = 0};
        for (size_t i = 0; i < src.sb.sz; i++) {
            ret &= comp_instr(
                ((char *)src.sb.buf)[i], &obj_code, inter, in_name, &loc
            );
        }
    }

    /* write code to perform the exit(0) syscall */
    /* set system call register to the desired exit code (0)
     * As 0 fits within 32-bit address space, this always succeeds. */
    inter->set_reg(inter->reg_arg1, 0, &obj_code, NULL);
    /* perform a system call */
    inter->syscall(&obj_code, inter->sc_exit);

    BFCError e;

    /* now, obj_code size is known, so we can write the headers and padding */
    ret &= write_headers(out_fd, tape_blocks, inter, obj_code.sz, out_name);
    /* finally, write the code itself. */
    if (!write_obj(out_fd, obj_code.buf, obj_code.sz, &e)) {
        e.file = out_name;
        display_err(e);
        ret = false;
    }

    /* check if any unmatched loop openings were left over. */
    for (size_t i = 0; i < jump_stack.next; i++) {
        display_err((BFCError){
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
 * `bf_compile`. If an error occurs, it returns the BfErrorId left-shifted by 1,
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
