<!--
SPDX-FileCopyrightText: 2024 Eli Array Minkoff

SPDX-License-Identifier: 0BSD
-->

# Eli Array Minkoff's BFC

A non-optimizing compiler for brainfuck, written in C.

Outputs an x86_64 ELF executable that uses Linux system calls for I/O.

Portability to other *target* platforms is outside of the scope of this project,
but it should be possible to compile and run `eambfc` itself on any POSIX\*
system with a C99 compiler. Please open an issue if that is not the case.

\* *Specifically POSIX.1-2008 with C-Language Development Utilities*

I am not an experienced C programmer, and this is an attempt to gain practice by
writing something somewhat simple yet not trivial.

Currently, it's working in the sense that it compiles the code properly on
`amd64` systems, but there a host of things to do before the project can be
considered complete - see [the To-do section](#to-do).

## Usage

```
Usage: eambfc [options] <program.bf> [<program2.bf> ...]

 -h        - display this help text
 -q        - don't print compilation errors.
 -k        - keep files that failed to compile for debugging
 -m        - Move ahead to the next file instead of quitting if a
             file fails to compile
 -e ext    - (only provide once) use 'ext' as the extension for
             source files instead of '.bf'.
             (This program will remove this at the end of the input
             file to create the output file name)

Remaining options are treated as source file names. If they don't
end with '.bf' (or the extension specified with '-e'), the program
will abort.

```

## Building and Installing

```sh
# Build eambfc
make
# install eambfc to /usr/local with sudo
sudo make install
# clean previous build and build with an alternative compiler
make clean; make CC=tcc
# install to an alternative path
make PREFIX="$HOME/.local" install
```

## To-do

### Core functionality

* [x] successfully compile "sub-bf" - brainfuck without `[` and `]`
* [x] successfully add the needed ELF headers to be able to run compiled code
* [x] successfully compile `[` and `]` to x86_64 machine code

### Extra goals

#### Completed

* [x] remove unused macros and definitions from header files
* [x] account for umask when creating a file
* [x] delete output file if compilation fails
* [x] better command-line interface for the compiler
* [x] add the ability to compile multiple source files in one run
* [x] add a command-line argument to continue on if a source file fails to build
* [x] add a command-line argument to disable deletion of failed files
  * [x] write headers if an instruction fails to compile and that option is set
* [x] address portability issues
  * [x] writing Ehdr and Phdr structs and using their sizes should be avoided
  * [x] ensure multi-byte values are written in an endian-agnostic manner.
  * [x] Bundle `elf.h` - its not always available on POSIX+C99 systems
  * [x] replace the Makefile with a better, more portable one

#### In Progress

* [ ] support printing multiple error messages

#### Planned

* [ ] automatic testing of brainfuck source files in `test/` directory
  * Should be done after the Makefile has been made more portable.
* [ ] make some hard-coded values (like tape size) configurable when building

#### Under Consideration

* [ ] Consider possible small optimizations, like compiling `[-]` as set 0
