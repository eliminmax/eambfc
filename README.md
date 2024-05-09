<!--
SPDX-FileCopyrightText: 2024 Eli Array Minkoff

SPDX-License-Identifier: 0BSD
-->

# Eli Array Minkoff's BFC

A non-optimizing compiler for brainfuck, written in C.

Outputs an x86_64 ELF executable that uses Linux system calls. Portability to other target platforms is outside of the scope of this project at this time.

I am not an experienced C programmer, and this is an attempt to gain practice by writing something somewhat simple yet not trivial.

Currently, it's working in the sense that it compiles the code properly on `amd64` systems, but there a a host of things to do before the project can be considered complete - see [the To-do section](#to-do).

## Usage

`./eambfc input.bf output`

## Building

Use of `gcc` as the C compiler is currently hard-coded into the Makefile. I plan on changing that.

The following external headers are used:

| Header          | Source/standard |
|-----------------|-----------------|
| `elf.h`         | (GNU C Library) |
| `fcntl.h`       | (POSIX)         |
| `stdint.h`      | (C99)           |
| `stdio.h`       | (C99)           |
| `string.h`      | (C99)           |
| `sys/types.h`   | (POSIX)         |
| `sys/stat.h`    | (POSIX)         |
| `unistd.h`      | (POSIX)         |

## To-do

* [ ] automatic testing of brainfuck source files in test/ directory
* [ ] replace the Makefile with a better, more portable one
* [ ] better command-line interface for the compiler
* [ ] account for umask when creating a file
* [ ] delete output file if compilation fails
  * should probably have a command-line argument to disable this for debugging purposes.
* [ ] add the ability to compile multiple source files in one run
* [ ] (possibly) add an option to generate headers to enable debugging with GDB
  * this one might be a bad idea.
* [ ] address portability issues 
  * compiled programs may not be portable, but it should be possible to compile and/or run `eambfc` itself on any POSIX+C99 system, ideally.
