<!--
SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff

SPDX-License-Identifier: GPL-3.0-only
-->

# Eli Array Minkoff's BFC (C Version)

An optimizing compiler for brainfuck, written in C for Unix-like systems.

<!-- __BACKENDS__ add backend to list of supported backends here -->
Output 64-bit ELF executables that uses Linux system calls for I/O. Currently,
it has x64_64, arm64, s390x, and riscv64 backends.

I started this as an inexperienced C programmer, and this was originally an
attempt to gain practice by writing something somewhat simple yet not trivial.
As it went on, I added more and more features, created
[a Rust rewrite](https://github.com/eliminmax/eambfc-rs), and have maintained
feature parity between them.

<!-- vim-markdown-toc GFM -->

* [Usage](#usage)
* [Dialect](#dialect)
* [Supported platforms](#supported-platforms)
  * [Non-portable functionality](#non-portable-functionality)
    * [Unit tests](#unit-tests)
* [Building and Installing](#building-and-installing)
* [Development Process](#development-process)
  * [Testing](#testing)
* [Dependencies](#dependencies)
  * [Debian dependencies](#debian-dependencies)
  * [Non-Debian Dependencies](#non-debian-dependencies)
* [Legal Stuff and Code Origins](#legal-stuff-and-code-origins)

<!-- vim-markdown-toc -->

## Usage

The most basic way to use `eambfc` is to simply run it, passing files to compile
as command-line arguments:

```sh
eambfc foo.bf
```

It expects brainfuck source files to use the file extension `.bf`.

That will compile `foo.bf` into an ELF executable file named `foo`.

If passed multiple files to compile, `eambfc` will compile them in the provided
order, stopping on the first error.

Compiled files have a tape size of 8 4-KiB blocks, for a total of 32 KiB, so any
program that works with Urban Müller's original implementation's 30 KB tape
should work fine.

| option     | effect                                                   |
|------------|----------------------------------------------------------|
| `-h`       | Display basic usage information, then exit               |
| `-V`       | Display version and copyright information, then exit     |
| `-j`       | Write JSON-formatted error messages to `stdout`          |
| `-q`       | Don't write error messages to `stderr`                   |
| `-O`       | Perform basic optimizations                              |
| `-c`       | Continue to the next file instead of aborting on failure |
| `-A`       | Display info about supported targets, and exit           |
| `-k`       | Don't delete files after failed compilation              |
| `-t count` | Use `count` 4-KiB blocks instead of the default 8        |
| `-e ext`   | Use `ext` instead of `.bf` as the source extension       |
| `-a arch`  | Use the `arch` backend instead of the default            |
| `-s suf`   | Append `suf` to the ends of output filenames             |


If compiled with long options enabled\*, the following options are the long
equivalents to the short options:

| short      | long                     |
|------------|--------------------------|
| `-h`       | `--help`                 |
| `-V`       | `--version`              |
| `-j`       | `--json`                 |
| `-q`       | `--quiet`                |
| `-O`       | `--optimize`             |
| `-c`       | `--continue`             |
| `-A`       | `--list-targets`         |
| `-k`       | `--keep-failed`          |
| `-t count` | `--tape-size=count`      |
| `-e ext`   | `--source-extension=ext` |
| `-a arch`  | `--target-arch=arch`     |
| `-s suf`   | `--output-suffix=suf`    |

\* *long options require `eambfc` to be compiled with
`'-D_GNU_SOURCE -DBFC_LONGOPT=1'` included in the `CFLAGS` passed to `make`.*

## Dialect

The brainfuck dialect supported by `eambfc` has the following semantics:

* 8-bit, wrapping cells
* tape size is set to 32 KiB unless `eambfc` is run with the `-t` flag, in which
  case it's the provided number of 4-KiB blocks
  * no bounds checking is performed, and the effects of out-of-bounds pointers
    is whatever the OS or compatibility environment running the binary happens
    to do
* I/O is unbuffered, using the `read` and `write` system calls (though most
  terminal drivers use line buffering for input)

## Supported platforms

It should be possible to compile and run `eambfc` itself for any system that
supports the APIs specified in the POSIX.1-2008 version of the POSIX standard,
using a compiler targeting any version of C from C99 on.

All Makefiles aim to use **only** functionality POSIX.1-2008 requires `make` to
support, which is very limited, and other than the `unit_test_driver` target
[as documented below](#unit-tests), no Makefile targets depend on non-POSIX
tools, libraries, or behavior, or behavior POSIX lists as
implementation-defined.

### Non-portable functionality

If the `BFC_LONGOPTS` macro is defined to have a nonzero value at compile time,
the GNU C library's `getopts_long` function is used instead of the
POSIX-standard `getopts`, to support GNU-style `--long-options`.

In the C Source code, GCC attributes and pragmas are used, but preprocessor
directives are used to ensure that they are only exposed if the `__GNUC__` macro
is defined.

#### Unit tests

Unit tests do not aim for the same level of portability as the rest of the
project.

* Unit tests use the CUnit framework for unit testing.

* Some unit tests use the LLVM disassembler through LLVM 19's C interface, to
  ensure that the codegen actually generates the right machine code - if I made
  a mistake in the codegen, it's more likely I'll make the same mistake when
  writing my own disassembler and it'll be uncaught, so I'd rather use something
  far more established instead.

* In the future, unit tests will use a 3rd-party library to test generation of
  JSON-formatted error messages for the same reason I used LLVM's disassembler.

## Building and Installing

Macros to enable or disable target architectures are defined in `config.h`, as
is a macro to select the default backend.

```sh
# Build eambfc
make

# clean previous build, build with glibc's getopt_long instead of POSIX getopt
make clean; make CFLAGS='-D _GNU_SOURCE -D BFC_LONGOPTS=1' eambfc

# Run the test suite
make test
# install eambfc to /usr/local with sudo
sudo make install
# clean previous build and build with an alternative compiler
make clean; make CC=clang
# install to an alternative path
make PREFIX="$HOME/.local" install
```

## Development Process

I have a dev branch and a main branch. When I feel like it, and all of the tests
pass, I merge the dev branch into the main branch. The main branch is guaranteed
to have run `make CFLAGS='-Wall -Werror -Wextra' clean test` successfully on the
following platforms:

* Debian 12 amd64 with `qemu-binfmt/bookworm-backports` to run foreign binaries
* Debian 12 arm64
* FreeBSD 14.2 amd64 with Linuxulator for Linux syscall support

Features that are both documented in the help output and/or the `eambfc.1` man
page in the main branch are tested and working, though code that's part of WIP
features may be present, but not activated.

Once a version is tagged, the tag will continue to point to that specific commit
forever.

The only thing promised about the dev branch is that it has successfully been
tested with `make clean test` on one of my personal systems before being pushed
to GitHub.

When developing, I have `core.hookspath` set to `.githooks/`, so that the git
hooks used can be checked into the git repository. There is a pre-commit hook
to check code quality with various linters, a pre-push commit that validates
that `make clean test` runs successfully.

### Testing

There are 2 main parts to the testing: CLI tests and unit tests.

CLI tests are run with `tests/test_driver`, which is written to work on all
supported platforms.

They test `eambfc` using its command-line interface to ensure it works as it
should. Some tests require the host system to support output binaries, so it
detects which output formats are supported at runtime, and skips tests for
unsupported formats.

Unit tests are run with `unit_test_driver`, which does not aim to hold up the
same level of portability as the rest of the project. It uses the CUnit
framework to run the tests, and uses LLVM's C interface to validate codegen,
as explained [above](#unit-tests).

Additionally, the `justfile` has rules to use various static and dynamic
analysis tools, and githooks are used to ensure that they all run successfully
before anything is merged into main.

## Dependencies

Developtment is done on Debian Bookworm with Backports enabled. Some development
tooling is not portalbe to other environments. Most development/testing
utilities are installed from the Debian repos:

### Debian dependencies

The following packages are used in testing or release automation:

* `binutils`
* `clang-19`
* `clang-format-19`
* `clang-tools-19`
* `codespell`
* `coreutils`
* `devscripts`
* `findutils`
* `gawk`
* `gcc-aarch64-linux-gnu`
* `gcc-i686-linux-gnu`
* `gcc-mips-linux-gnu`
* `gcc-s390x-linux-gnu`
* `gcc`
* `git`
* `gzip`
* `libasan6`
* `libcunit1-dev`
* `libcjson-c-dev`
* `libubsan1`
* `llvm-19-dev`
* `make`
* `musl-tools`
* `parallel`
* `qemu-user-binfmt`
  * I use the backports version, as the stable version segfaults seemingly at
    random with `s390x` binaries
* `sed`
* `shellcheck`
* `tar`
* `valgrind`
* `xz-utils`

With the `equivs` package installed, the following control file will generate a
metapackage depending on them:

```debcontrol
Section: misc
Priority: optional
Homepage: https://github.com/eliminmax/eambfc
Standards-Version: 3.9.2

Package: eambfc-dev-deps
Version: 4.0.0
Maintainer: Eli Array Minkoff <eli@planetminkoff.com>
Depends: awk, binutils, clang-19, clang-format-19,
 clang-tools-19, codespell, coreutils, devscripts, findutils, gcc,
 gcc-aarch64-linux-gnu, gcc-i686-linux-gnu, gcc-mips-linux-gnu,
 gcc-s390x-linux-gnu, git, gzip, libasan6, libcunit1-dev, libubsan1,
 libcjson-dev, llvm-19-dev, make, musl-tools, parallel, pkg-config,
 qemu-user-binfmt (>= 1:9.0.0), sed, shellcheck, tar, valgrind,
 xz-utils
Suggests: clangd-19
Description: Dependencies of eambfc's development workflow
 While eambfc is written with portability to POSIX systems
 as an explicit goal, the test suite makes heavy use of 3rd-party
 tools.
 .
 This metapackage depends on Debian packages which comprise the
 majority of those tools, only excluding those not packaged by Debian
 at all.
```

Alternatively, run the following as root:

```sh
apt install awk binutils clang-19 clang-format-19 clang-tools-19 codespell \
    coreutils devscripts findutils gcc gcc-aarch64-linux-gnu \
    gcc-i686-linux-gnu gcc-mips-linux-gnu gcc-s390x-linux-gnu git gzip \
    libasan6 libcunit1-dev libubsan1 libjson-c-dev llvm-19-dev make musl-tools \
    parallel pkg-config qemu-user-binfmt/bookworm-backports sed shellcheck tar \
    valgrind xz-utils
```

### Non-Debian Dependencies

* [The Zig compiler](https://ziglang.org)
  * used for its built-in C compiler and cross-compilation support
* [Ron Yorston's Public Domain POSIX make](https://frippery.org/make)
  * used to check for non-portable make functionality
* [reuse helper tool >= 5.0.0](https://git.fsfe.org/reuse/tool)
  * newer than Debian-packaged version, used to validate license data
* [cppcheck](https://github.com/danmar/cppcheck)
  * newer than Debian-packaged version, and supports more checks
* [just](https://github.com/casey/just)
  * command runner used to drive testing and release automation

## Legal Stuff and Code Origins

`eambfc` as a whole is licensed under the GNU General Public License version 3.
Some individual components of the source code, infrastructure, and test assets
are licensed under other compatible licenses, mainly the Zero-Clause BSD
license, which is a public-domain-equivalent license.

Files have licensing information encoded in accordance with the REUSE
Specification, using SPDX headers to encode the copyright info in a manner that
is both human and machine readable.

Some brainfuck test programs include snippets of sample code taken from the
esolangs.org pages
[brainfuck algorithms](https://esolangs.org/wiki/Brainfuck_algorithms) and
[brainfuck constants](https://esolangs.org/wiki/Brainfuck_constants), which are
available under the CC0 public-domain-equivalent license.

One test uses a modified form of the brainfuck implementation of
[colortest](https://github.com/eliminmax/colortest), which is a different hobby
project of mine.

An algorithm to pick a RISC-V instruction sequence to set a register to an
immediate is adapted from the LLVM project.

Other than that, all content in this repository in my own original work, either
created for `eambfc`, or adapted from the `eambfc-rs` project in Rust.

All licenses used in any part of this repository are in the LICENSES/ directory,
and every file has an SPDX License header identifying the license(s) it's under,
either near the top of the file, or in an associated `.license` file.
