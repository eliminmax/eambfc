<!--
SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff

SPDX-License-Identifier: GPL-3.0-only
-->

# Eli Array Minkoff's BFC (original version)

**Also check out the
[Blazingly 🔥 fast 🚀 version](https://github.com/eliminmax/eambfc-rs), written
in Rust 🦀!**

An optimizing compiler for brainfuck, written in C for Unix-like systems.

Output 64-bit ELF executables that uses Linux system calls for I/O.
Currently, it has x64_64 and arm64 backends.

I started this as an inexperienced C programmer, and this was originally an
attempt to gain practice by writing something somewhat simple yet not trivial.
As it went on, I added more and more features, created a Rust rewrite, and have
maintained feature parity between them.

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
 -t count  - (only provide once) allocate <count> 4-KiB blocks for
             the tape. (defaults to 8 if not specified)
 -e ext    - (only provide once) use 'ext' as the extension for
             source files instead of '.bf'
             (This program will remove this at the end of the input
             file to create the output file name)
 -a arch   - compile for the specified architecture
             (defaults to x86_64 if not specified)**
 -s ext    - (only provide once) use 'ext' as the extension for
             compiled binaries (empty if not specified)
 -A        - list supported architectures and exit

* -q and -j will not affect arguments passed before they were.

** Optimization can make error reporting less precise.

Remaining options are treated as source file names. If they don't
end with '.bf' (or the extension specified with '-e'), the program
will raise an error.
```

## Supported platforms

It should be possible to compile and run `eambfc` itself on any POSIX\* system
with a C99 compiler. If that is not the case, it's a bug.

\* *Specifically POSIX.1-2008. Compilation requires the optional C-Language
Development Utilities, or at least something similar enough. It probably works
on systems that comply with any version of the POSIX standard from POSIX.1-2001
on, and with any newer version of ISO standard C, but if it does not, it's not
considered a bug.*

## Building and Installing

Macros to enable or disable target architectures are defined in `config.h`, as
is a macro to select the default backend.

```sh
# Build eambfc
make
# Run the test suite
make test
# install eambfc to /usr/local with sudo
sudo make install
# clean previous build and build with an alternative compiler
make clean; make CC=tcc
# install to an alternative path
make PREFIX="$HOME/.local" install
```

## Development Process and Standards

I have a dev branch and a main branch. When I feel like it, and all of the tests
pass, I merge the dev branch into the main branch. The main branch is guaranteed
to have passed the full test suite on Debian 12 amd64, Debian 12 arm64 with
qemu-binfmt, and FreeBSD 14.2 amd64 with Linux binary support. Features that are
documented and exposed via command-line flags are tested, working, and complete,
though code that's part of WIP features may be present, but not activated.

The only thing promised about the dev branch is that it has successfully been
tested with `make clean test` on one of my systems before being pushed to
GitHub.

When developing, I have `core.hookspath` set to `.githooks/`, so that the git
hooks used can be checked into the git repository. There is a pre-commit hook
to check code quality with various linters, a pre-push commit that validates
that `make clean test` runs successfully.

### Standards compliance

All C code and Makefiles, and most shell scripts, target the POSIX.1-2008
standard and/or the ISO/IEC 9899:1999 standard (i.e. C99).

Some Makefile targets are not portable - they use `gcc`-specific flags and
features, and are hard-coded to call `gcc` rather than the user-specified C
compiler.

The `release.sh` and `run_lints.sh` scripts have a large number of extra
dependencies which they document, used for extra testing and linting, and for
building source tarballs.

All files in the main branch comply with version 3.3 of the REUSE specification
for licensing information.

### Test suite

The primary test suite consists of a series of brainfuck programs, and a script
to compare their actual behavior against their expected behavior. Because of
this, it must be able to run the created binaries, which means that it does not
work on systems that can't run ELF files for supported architectures with Linux
system calls. That said, I have successfully run the test suite in a FreeBSD VM
with [Linuxulator](https://docs.freebsd.org/en/books/handbook/linuxemu/)
enabled, and a Debian 12 "Bookworm" arm64 system with the `qemu-user-binfmt`
package installed.

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

Other `compat/elf.h` and the sample code from esolangs.org, all content in this
repository is my own original work.

All licenses used in any part of this repository are in the LICENSES/ directory,
and every file has an SPDX License header identifying the license(s) it's under,
either near the top of the file, or in an associated `.license` file.

