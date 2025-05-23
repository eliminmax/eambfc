#!/usr/bin/env python3
"""Validate the test ELF files

SPDX-FileCopyrightText: 2025 Eli Array Minkoff

SPDX-License-Identifier: 0BSD

This file s meant to be used by anyone concerned by the small binaries included
in this repo, to demonstrate that they are harmless.

The code is written to minimize different code paths, to make it simpler to
follow.

The structure of an ELF file is described in the ELF(5) man page in the Linux
man-pages collection, and the specific constants used are defined in GLIBC's
elf.h header file.

The system call ABI is described in the SYSCALL(2) man page, also from the
Linux man-pages collection, and a table of syscall numbers by architecture is
available at https://gpages.juszkiewicz.com.pl/syscalls-table/syscalls.html.

My approach to creating tiny ELF files, and the files these are based on, can
be found in my tiny-clear-elf project at
https://github.com/eliminmax/tiny-clear-elf.

I used radare2's rasm2 to validate the machine code instructions themselves.
See https://rada.re/n/radare2.html for info about that project.

Machine code bytes are commented with the command to check that they are the
instructions that I claim them to be using rasm2 yourself.
"""
from pathlib import PosixPath as Path
import sys


def gen_headers(
    target_id: int, instr_len: int, is64bit=True, be=False, flags: int = 0
) -> bytes:
    """Return the bytes of the ELF header and program header table"""

    LOAD_ADDR = 0x10000

    # values dependent on ELF size
    # EHDR_SIZE is the size in bytes of the EHDR, PHTB_SIZE is the size in
    # bytes of the program header table, and ADDR_SIZE is the size of a memory
    # address or file offset.
    EHDR_SIZE, PHTB_SIZE, ADDR_SIZE = (64, 56, 8) if is64bit else (52, 32, 4)
    # combined header size
    HEADER_SIZE = EHDR_SIZE + PHTB_SIZE

    def int_encode(val: int, length: int) -> bytes:
        """internal helper function to serialize integer values into bytes"""
        return val.to_bytes(length, byteorder="big" if be else "little")

    # EHDR
    # e_ident
    ret = b"\x7fELF" + (
        bytes([2 if is64bit else 1])  # ELFCLASS64 or ELFCLASS32
        + bytes([2 if be else 1])  # ELFDATA2MSB, ELFDATA2LSB
        + bytes([1])  # EV_CURRENT
        + bytes([0, 0])  # ELFOSABI_NONE, no ABI version specified
        + bytes([0] * 7)  # padding bytes
    )
    # e_type
    ret += int_encode(2, 2)  # ET_EXEC
    # e_machine
    ret += int_encode(target_id, 2)
    # e_version
    ret += int_encode(1, 4)  # EV_CURRENT
    # e_entry
    # entry follows headers
    ret += int_encode(LOAD_ADDR + HEADER_SIZE, ADDR_SIZE)
    # e_phoff
    # PHDR table is right after the EHDR
    ret += int_encode(EHDR_SIZE, ADDR_SIZE)
    # e_shoff
    ret += int_encode(0, ADDR_SIZE)  # no shdr table
    # e_flags
    ret += int_encode(flags, 4)
    # e_ehsize
    ret += int_encode(EHDR_SIZE, 2)
    # e_phentsize
    ret += int_encode(PHTB_SIZE, 2)
    # e_phnum
    ret += int_encode(1, 2)  # 1 phdr table entry
    # e_shentsize
    ret += int_encode(0, 2)  # no shdr table
    # e_shnum
    ret += int_encode(0, 2)  # no shdr table
    # e_shstrndx
    ret += int_encode(0, 2)  # no shdr table

    # PHDR entry
    # p_type
    ret += int_encode(1, 4)  # PT_LOAD
    # p_flags is moved to the second-to-last spon in 32-bit ELF files
    if is64bit:
        # p_flags
        ret += int_encode((1 << 0) | (1 << 2), 4)  # PF_X | PF_R
    # p_offset
    ret += int_encode(0x0, ADDR_SIZE)  # offset in file segment is loaded from
    # p_vaddr
    ret += int_encode(LOAD_ADDR, ADDR_SIZE)  # address in virtual memory
    # p_paddr
    ret += int_encode(0x0, ADDR_SIZE)  # (unused) address in physical memory
    # p_filesz
    ret += int_encode(HEADER_SIZE + instr_len, ADDR_SIZE)  # size in file
    # p_memsz
    ret += int_encode(HEADER_SIZE + instr_len, ADDR_SIZE)  # size in memory
    if not is64bit:
        # p_flags
        ret += int_encode((1 << 0 | 1 << 2), 4)  # PF_X | PF_R
    # p_align
    ret += int_encode(2, ADDR_SIZE)  # segment alignment

    return ret


# __BACKENDS__ add a block to validate the execfmt_support binary for the
# backend


"""
validate arm64 bytes with radare2:
rasm2 -aarm -b64 -D 'a80b8052 000080d2 010000d4'
"""
ARM64_INSTRUCTIONS = (
    # mov w8, 0x5d (set syscall to exit)
    bytes.fromhex("a80b8052")
    # mov x0, 0 (set arg1 to 0)
    + bytes.fromhex("000080d2")
    # svc 0 (syscall)
    + bytes.fromhex("010000d4")
)

"""validate i386 bytes with radare2:
rasm2 -ax86 -b32 -D '6a01 58 31d2 cd80'
"""
I386_INSTRUCTIONS = (
    # push 1 (push exit syscall number to stack)
    bytes.fromhex("6a01")
    # pop eax (pop top of stack into syscall register)
    + bytes.fromhex("58")
    # xor edx, edx (zero out arg1 register)
    + bytes.fromhex("31d2")
    # int 0x80 (syscall)
    + bytes.fromhex("cd80")
)

"""
validate riscv64 instruction bytes with radare2:
rasm2 -ariscv -b64 -D '9308d005 0145 73000000'
"""
RISCV64_INSTRUCTIONS = (
    # li a7, 93 (set syscall to exit)
    bytes.fromhex("9308d005")
    # li a0, 0 (set syscall to exit)
    + bytes.fromhex("0145")
    # ecall (syscall)
    + bytes.fromhex("73000000")
)

"""
validate s390x instruction bytes with radare2:
rasm2 -as390 -b64 -D 'a7290000 0a01'
"""
S390X_INSTRUCTIONS = (
    # lghi r2, 0 (set arg1 to 0)
    bytes.fromhex("a7290000")
    # svc 1 (perform syscall #1 (i.e. exit))
    + bytes.fromhex("0a01")
)

"""
validate x86_64 instruction bytes with radare2:
rasm2 -ax86 -b64 -D '6a3c 58 31ff 0f05'
"""
X86_64_INSTRUCTIONS = (
    # push 0x3c (push exit syscall number to stack)
    bytes.fromhex("6a3c")
    # pop rax (pop top of stack into syscall register)
    + bytes.fromhex("58")
    # xor edi, edi (zero out arg1 register)
    + bytes.fromhex("31ff")
    # syscall (syscall)
    + bytes.fromhex("0f05")
)


arm64_bytes = Path(__file__).parent.joinpath("arm64").read_bytes()
expected_arm64_bytes = (
    # 183 is EM_AARCH64
    gen_headers(183, len(ARM64_INSTRUCTIONS))
    + ARM64_INSTRUCTIONS
)

i386_bytes = Path(__file__).parent.joinpath("i386").read_bytes()
expected_i386_bytes = (
    # 3 is EM_386
    gen_headers(3, len(I386_INSTRUCTIONS), is64bit=False)
    + I386_INSTRUCTIONS
)

riscv64_bytes = Path(__file__).parent.joinpath("riscv64").read_bytes()
expected_riscv64_bytes = (
    # 243 is EM_RISCV, 5 is EF_RISCV_RVC | EF_RISCV_FLOAT_ABI_DOUBLE
    gen_headers(243, len(RISCV64_INSTRUCTIONS), flags=5)
    + RISCV64_INSTRUCTIONS
)

s390x_bytes = Path(__file__).parent.joinpath("s390x").read_bytes()
expected_s390x_bytes = (
    # 22 is EM_S390
    gen_headers(22, len(S390X_INSTRUCTIONS), be=True)
    + S390X_INSTRUCTIONS
)

x86_64_bytes = Path(__file__).parent.joinpath("x86_64").read_bytes()
expected_x86_64_bytes = (
    # 62 is EM_X86_64
    gen_headers(62, len(X86_64_INSTRUCTIONS))
    + X86_64_INSTRUCTIONS
)

fails = 0

if arm64_bytes != expected_arm64_bytes:
    print("MISMATCH! arm64_bytes != expected_arm64_bytes")
    fails += 1

if i386_bytes != expected_i386_bytes:
    print("MISMATCH! i386_bytes != expected_i386_bytes")
    fails += 1

if riscv64_bytes != expected_riscv64_bytes:
    print("MISMATCH! riscv64_bytes != expected_riscv64_bytes")
    print(f"{expected_riscv64_bytes.hex()=}")
    print(f"         {riscv64_bytes.hex()=}")
    fails += 1

if s390x_bytes != expected_s390x_bytes:
    print("MISMATCH! s390x_bytes != expected_s390x_bytes")
    fails += 1

if x86_64_bytes != expected_x86_64_bytes:
    print("MISMATCH! x86_64_bytes != expected_x86_64_bytes")
    fails += 1

# exit code is the number of mismatches
sys.exit(fails)
