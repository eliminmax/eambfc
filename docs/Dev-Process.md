<!--
SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff

SPDX-License-Identifier: GPL-3.0-only
-->

# Development Process

There's a dev branch and a main branch. When changes are more-or-less complete,
and all of the tests pass, the dev branch may be merged into the main branch.
The main branch is guaranteed to have run
`make CFLAGS='-Wall -Werror -Wextra' clean test` successfully on the following
platforms:

* Debian 12 amd64 with `qemu-binfmt/bookworm-backports` to run foreign binaries
* Debian 12 arm64
* FreeBSD 14.2 amd64 with Linuxulator for Linux syscall support

Features that are both documented in the `eambfc -h` output and/or the
`eambfc.1` man page in the main branch are tested and working, though code
that's part of WIP features may be present, but not documented there and/or not
actually run.

Once a version is tagged, the tag will continue to point to that specific commit
forever.

The only thing promised about the dev branch is that it has successfully been
tested with `make clean test` on one of my personal systems before being pushed
to GitHub, enforced with a `git hook`.

When developing, `core.hookspath` is set to `.githooks/`, so that the git hooks
that are used can be checked into the git repository.

## Testing

There are 2 main parts to the testing: CLI tests and unit tests.

CLI tests are run with `tests/test_driver`, which is written to work on all
supported platforms. What it does is use POSIX-compliant `fork` and `exec` calls
to `eambfc`, capturing its output and exit code and validating that it behaves
properly in various cases.

Unit tests are run with `unit_test_driver`, which does not aim to hold up the
same level of portability as the rest of the project. It uses the CUnit
framework to run the tests, and uses LLVM's C interface to validate codegen, as
well as `json-c` to validate the structure of JSON-encoded error messages.

Additionally, the `justfile` has rules to use various static and dynamic
analysis tools, and git hooks are used to ensure that they all run successfully
before anything is merged into the main branch.

## Dependencies

Developtment is done on Debian Bookworm with Backports enabled, and some
development tooling is not portalbe to other environments. Most development and
testing utilities are installed from the Debian repository, though some are
installed using other means.

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
 clang-tools-19, codespell, coreutils, devscripts, findutils
 gcc, gcc-aarch64-linux-gnu, gcc-i686-linux-gnu, gcc-mips-linux-gnu,
 gcc-s390x-linux-gnu, git, gzip, libasan6, libcunit1-dev, libubsan1,
 libcjson-dev, llvm-19-dev, make, musl-tools, parallel,
 pkg-config, qemu-user-binfmt (>= 1:9.0.0), sed, shellcheck, tar, valgrind,
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
  * used for its built-in C compiler
* [Ron Yorston's Public Domain POSIX make](https://frippery.org/make)
  * used to check for non-portable make functionality
* [reuse helper tool >= 5.0.0](https://git.fsfe.org/reuse/tool)
  * newer than Debian-packaged version, used to validate license data
* [cppcheck](https://github.com/danmar/cppcheck)
  * newer than Debian-packaged version, and supports more checks
* [just](https://github.com/casey/just)
  * command runner used to drive testing and release automation

