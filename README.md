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

To build, simply run `make`. No `make install` target is defined. If you want to add it into your `PATH`, you'll need to do that manually.

## To-do

### Core functionality

* [x] successfully compile "sub-bf" - brainfuck without the `[` and `]` instructions - to x86_64 machine code, using Linux system calls for `.` and `,`.
* [x] successfully add the needed ELF structure to be able to run compiled code
* [x] successfully compile `[` and `]` to x86_64 machine code

### Extra goals

#### Completed

* [x] remove unused macros and definitions from header files
* [x] account for umask when creating a file
* [x] delete output file if compilation fails

#### In Progress

* [ ] address portability issues - this one will be particularly difficult.
  * compiled programs may not be portable, but it should be possible to compile and/or run `eambfc` itself on any POSIX+C99 system, ideally.
  * specific portability issues:
    * [x] writing Ehdr and Phdr structs and using their sizes should be avoided, as struct alignment/padding is not portable.
    * [x] ensure multi-byte values are written in an endian-agnostic manner.
    * [x] `elf.h` is not present on all POSIX systems, and should be either provided or replaced.
    * [ ] replace the Makefile with a better, more portable one

#### Planned

* [ ] automatic testing of brainfuck source files in `test/` directory
* [ ] make some hard-coded values (like tape size) configurable when building `eambfc`
* [ ] better command-line interface for the compiler
* [ ] add a a command-line argument to disable deletion of failed files (for debugging purposes).
* [ ] add the ability to compile multiple source files in one run
