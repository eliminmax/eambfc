<!--
SPDX-FileCopyrightText: 2024 Eli Array Minkoff

SPDX-License-Identifier: 0BSD
-->

# Eli Array Minkoff's BFC

A non-optimizing compiler for brainfuck, written in C for Unix-like systems.

Outputs x86\_64 ELF executables that uses Linux system calls for I/O.

I started this as an inexperienced C programmer, and this was originally an
attempt to gain practice by writing something somewhat simple yet not trivial.

## Usage

```
Usage: eambfc [options] <program.bf> [<program2.bf> ...]

 -h        - display this help text and exit
 -V        - print version information and exit
 -j        - print errors in JSON format*
             (assumes file names are UTF-8-encoded.)
 -q        - don't print errors unless -j was passed*
 -O        - enable optimization**.
 -k        - keep files that failed to compile (for debugging)
 -c        - continue to the next file instead of quitting if a
             file fails to compile
 -e ext    - (only provide once) use 'ext' as the extension for
             source files instead of '.bf'
             (This program will remove this at the end of the input
             file to create the output file name)

* -q and -j will not affect arguments passed before they were.

** Optimization will mess with error reporting, as error locations
   will be location in the intermediate representation text, rather
   than the source code.

Remaining options are treated as source file names. If they don't
end with '.bf' (or the extension specified with '-e'), the program
will abort.

```

## Supported platforms

Portability to other *target* platforms is outside of the scope of this project,
but it should be possible to compile and run `eambfc` itself on any POSIX\*
system with a C99 compiler. If that is not the case, it's a bug.

\* *Specifically POSIX.1-2008. Compilation requires the optional C-Language
Development Utilities, or at least something similar enough.*

It probably works on systems that comply with any version of the POSIX standard
from POSIX.1-2001 on, and with any newer version of ISO standard C, but if it
does not, it's not a bug.

## Building and Installing

```sh
# Build eambfc
make
# rebuild eambfc with a different number of 4096-byte blocks for the tape size
make TAPE_BLOCKS=16  # default is 8
# rebuild eambfc so that it only prints 4 compiler errors
make MAX_ERROR=4  # default is 32
# Run the test suite
make test
# install eambfc to /usr/local with sudo
sudo make install
# clean previous build and build with an alternative compiler
make clean; make CC=tcc
# install to an alternative path
make PREFIX="$HOME/.local" install
```

## Development process and Standards

I have a dev branch and a main branch. When I feel like it, and all of the tests
pass, I merge the dev branch into the main branch. I work from multiple devices,
but only push changes from one. The main branch has been successfully built and
passed the full test suite on Debian 12 amd64, Debian 12 arm64 with qemu-binfmt,
and FreeBSD 14.1 amd64 with Linux binary support. Features that are documented
and exposed via command-line flags are tested, working, and complete, though
code that's part of WIP features may be present, but not activated.

The dev branch has no guarantees of any kind. It may have untested code, fatal
bugs, invalid code that won't compile at all, failing tests, undefined behavior,
improperly-formatted files, or other problems. Do not use it.

### Standards compliance

Other than some tests, all C code, shell scripts, and Makefiles target the
POSIX.1-2008 standard and/or the ISO/IEC 9899:1999 standard (i.e. C99).

Source code must also comply with version 3.0 of the REUSE specification.

### Test suite

The test suite consists of a series of brainfuck programs, and a script to
compare their actual behavior against their expected behavior. Because of this,
it must be able to run the created binaries, which means that it does not work
on systems that can't run x86-64 ELF files with Linux system calls. That said, I
have successfully run the test suite in a FreeBSD VM with
[Linuxulator](https://docs.freebsd.org/en/books/handbook/linuxemu/) enabled, and
a Debian 12 "Bookworm" arm64 system with the `qemu-user-binfmt` package
installed.

### Code style

The following are the formatting I follow for source code:

* C: C89-style comments (i.e. `/* comment */`, not `// comment`)
* 80 character maximum per line, regardless of language.
  * tabs are 4 wide when they appear.
* Indentation:
  * General: 4 spaces for indentation, except in the following cases:
    * C: `case`s within `switch` statements are half indented
    * Makefiles use 8-wide tabs due to the constraints of the format
    * Markdown files use 2 spaces due to the constraints of the format
* C: Open braces are on the same line as the function signature/conditional/etc.
* C: Multi-line comments should have an asterisk at the start of each line
* Names:
  * function names are `camelCase`.
  * struct names are `PascalCase`.
  * variables names and non-struct `typedef`ed types are `snake_case`.
  * macros are `SCREAMING_SNAKE_CASE`.
* C: `include`s are split into sections starting with one of the following:
  * `/* C99 */`: the header is defined by the C99 standard
    * either the C99 standard or the POSIX.1-2008 must require it to be present
    * POSIX.1-2008 extensions to the header's contents may be used.
  * `/* POSIX */`: the header is defined by and required by POSIX.1-2008
    * The C-Language Development extensions are assumed to be present.
  * `/* internal */`: the header is provided within the `eambfc` source tree.

Most of the code in this repository was written specifically for this project,
and follows the formatting and style rules. Code originally from other projects
may or may not be adapted to fit some or all of the formatting and style rules.

Brainfuck source code in the `test/` directory is the exception - it has no
formatting rules or style guides, but the code should include commentary to
explain what it's doing, how, and, if not written for this project, where it
came from.

## Ideas for future versions

### In Progress

* do some small optimizations, like compiling `[-]` as 'set 0'

### Under Consideration

* extend the jump stack as needed instead of erroring out at > 64 nested

## Legal Stuff

`eambfc` as a whole is licensed under the GNU General Public License version 3.
Some individual components of the source code, infrastructure, and test assets
are licensed under other compatible licenses, mainly the Zero-Clause BSD license
(a public-domain-equivalent license).

Some brainfuck test programs include snippets of sample code taken from the
esolangs.org pages
[brainfuck algorithms](https://esolangs.org/wiki/Brainfuck_algorithms) and
[brainfuck constants](https://esolangs.org/wiki/Brainfuck_constants), which are
available under the CC0 public-domain-equivalent license.

Other than the macros and `typedefs` in `compat/elf.h` and the sample code
from esolangs.org, all content in this repository is my original work.

All licenses used in any part of this repository are in the LICENSES/ directory,
and every file has an SPDX License header identifying the license(s) it's under,
either near the top of the file, or in an associated `.license` file.
