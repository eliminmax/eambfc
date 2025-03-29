#!/usr/bin/env -S just -f

# SPDX-FileCopyrightText: 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

# __BACKENDS__: add backend here
backends := 'arm64 riscv64 s390x x86_64'
build_name := "eambfc-" + trim(shell("git describe --tags"))
src_tarball_name := build_name + "-src.tar"
src_tarball_path := justfile_dir() / "releases" / src_tarball_name
backend_sources := replace_regex(backends, '\b([^ ]+)\b', 'backend_${1}.c')
unibuild_files := (
    'serialize.c compile.c optimize.c ' + backend_sources +
    ' err.c util.c parse_args.c main.c'
)

# aligning it like this was not easy, but it sure is satisfying
gcc_strict_flags := (
    '-std=c99 -D_POSIX_C_SOURCE=200908L -O3 -fanalyzer -Wall -Wextra -Werror ' +
    '-Wpedantic -Wformat-truncation=2 -Wduplicated-branches -Wshadow -Wundef ' +
    '-Wformat-overflow=2 -Wformat-signedness -Wbad-function-cast -Winit-self ' +
    '-Wnull-dereference -Wredundant-decls -Wduplicated-cond -Warray-bounds=2 ' +
    '-Wuninitialized -Wlogical-op -Wwrite-strings -Wformat=2 -Wunused-macros ' +
    '-Wcast-align=strict -Wcast-qual -Wtrampolines -Wvla'
)

gcc_ubsan_flags := gcc_strict_flags + ' ' + (
    '-fsanitize=leak,address,undefined -fno-sanitize-recover=all -g3'
)

gcc_longopts_flags := '-D_GNU_SOURCE -DBFC_LONGOPTS=1'

export MAKEFLAGS := '-se'
export BFC_DONT_SKIP_TESTS := '1'

# GENERAL

[doc("Build the basic `eambfc` binary with `make`")]
[group("general")]
eambfc:
    make eambfc

[doc("test a release tarball of `eambfc`, and build a ")]
[group("general")]
release-build: scan-build cppcheck-full tarball pdpmake valgrind_test \
    (valgrind_test '-D_GNU_SOURCE -DBFC_LONGOPTS=1 -O2 -g3')
    #!/bin/sh
    set -eux
    for o_lvl in 0 1 2 3 s g z; do
        # compile with a bunch of different compilers/setups
        printf 'gcc\nmusl-gcc\nclang-19\nzig cc\n' |\
            parallel -I_cc just --no-deps test_build \
                _cc "-O$o_lvl" -Wall -Werror -Wextra -pedantic -std=c99
        # build for specific architectures now - these were chosen to catch bugs
        # that are 32-bit-only, 64-bit-only, and/or endianness-specific.
        printf '%s-linux-gnu-gcc\n' i686 aarch64 mips s390x |\
            LDFLAGS=-static parallel -I_cc just --no-deps test_build \
                _cc "-O$o_lvl" -Wall -Werror -Wextra -pedantic -std=c99 -static
    done
    # this has caught loss of *const qualifier that the bigger compilers did not
    just --no-deps test_build tcc -Wall -Wwrite-strings -Werror

    # if none of those tests hit any issues, make the compressed archives and
    # the actual build
    gzip -9 --keep {{quote(src_tarball_name)}}
    xz -9 --keep {{quote(src_tarball_name)}}
    build_dir="$(mktemp -d "/tmp/eambfc-release-XXXXXXXXXX")"
    cd "$build_dir"
    tar --strip-components=1 -xf {{quote(src_tarball_path)}}
    make CC=gcc CFLAGS="-O3 -flto" LDFLAGS="-flto"
    strip eambfc
    mv eambfc {{quote(justfile_dir() / 'releases' / build_name)}}
    cd {{quote(justfile_dir() / 'releases')}}
    rm -rf "$build_dir"

[doc("Create release tarball")]
[group("general")]
tarball: pre-tarball-checks
    #!/bin/sh -xeu
    make clean
    make release.make
    sed '/git commit: /s/"/"source tarball from /' -i version.h
    mkdir -p releases
    # finding the way to exclude files this way was not easy - it's under the
    # gitglossary docs for pathspec, but until I found an example, I still
    # couldn't get it quite right.
    # the glossary is at <https://git-scm.com/docs/gitglossary>, and the
    # example was in the StackOverflow question at
    # <https://stackoverflow.com/q/57730171>.
    #
    # the first --transform flag substitutes the Makefile with release.make
    # the second one prepends "(build_name)-src/" to all files in the tarball
    #
    # version.h and the release Makefile are included explicitly as they're
    # not passed by `git ls-files`.
    git ls-files -z --exclude-standard -- ':(attr:!no-tar)' | \
        xargs -0 tar --transform='s/release[.]make/Makefile/' \
            --transform='s#^#{{build_name}}-src/#' \
            -cf 'releases/{{src_tarball_name}}' version.h release.make


# TESTS - static analysis, unit tests, cli tests, etc.

[doc("Run `eambfc` through GCC's static analyzer")]
[group("tests")]
strict-gcc:
    gcc {{ gcc_strict_flags }} {{ unibuild_files }} -o /dev/null
    gcc {{ gcc_strict_flags }} {{ gcc_longopts_flags }} {{ unibuild_files }} \
        -o /dev/null

[doc("Run the full project through the `cppcheck` static analyzer")]
[group("tests")]
cppcheck-full:
    cppcheck -q --std=c99 -D__GNUC__ --error-exitcode=1 --platform=unspecified \
        --check-level=exhaustive --enable=all --disable=missingInclude \
        --suppress=checkersReport {{ unibuild_files }}

[doc("Run `eambfc` through LLVM's `scan-build` static analyzer")]
[group("tests")]
scan-build:
    scan-build-19 --status-bugs make CFLAGS=-O3 clean eambfc
    scan-build-19 --status-bugs make \
        CFLAGS={{quote('-O3 ' + gcc_longopts_flags) }} clean eambfc
    scan-build-19 --status-bugs make CFLAGS=-O3 unit_test_driver
    make clean

[doc("Build `eambfc` with **UBsan**, and run through the test suite")]
[group("tests")]
ubsan-test: alt-builds-dir test_driver
    gcc {{ gcc_ubsan_flags }} {{ unibuild_files }} -o alt-builds/eambfc-ubsan
    just --no-deps test alt-builds/eambfc-ubsan
    gcc {{ gcc_ubsan_flags }} {{ unibuild_files }} {{ gcc_longopts_flags }} \
        -o alt-builds/eambfc-ubsan-longopts
    just --no-deps test alt-builds/eambfc-ubsan-longopts

[doc("Build and test `eambfc` with 64-bit integer fallback hackery")]
[group("tests")]
int-torture-test: alt-builds-dir test_driver
    gcc -D INT_TORTURE_TEST=1 {{gcc_ubsan_flags}} -Wno-pedantic \
        {{ unibuild_files }} -o alt-builds/eambfc-itt
    just --no-deps test alt-builds/eambfc-itt
    gcc -D INT_TORTURE_TEST=1 {{gcc_ubsan_flags}} -Wno-pedantic \
        {{ gcc_longopts_flags }} {{ unibuild_files }} \
            -o alt-builds/eambfc-itt-longopts
    just --no-deps test alt-builds/eambfc-itt-longopts

[doc("Test provided `eambfc` build using its cli")]
[group("tests")]
[working-directory('./tests')]
test eambfc="eambfc": test_driver
    env EAMBFC={{ quote(join(justfile_dir(), eambfc)) }} ./test_driver

[doc("Run through the unit tests")]
[group("tests")]
unit-test: unit_test_driver
    ./unit_test_driver



# LINTS - check style and quality of individual files and the project as a whole

[doc("Check repo for REUSE compliance")]
[group("lints")]
reuse:
    reuse lint

[doc("run cppcheck with options suitable for standalone files")]
[group("lints")]
cppcheck-single +files:
    cppcheck -q --std=c99 -D__GNUC__ \
      --platform=unspecified --enable=all \
      --disable=missingInclude,unusedFunction --error-exitcode=1 \
      --check-level=exhaustive --suppress=checkersReport {{ files }}

[doc("Check that SPDX-FileCopyrightText has current year")]
[group("lints")]
[positional-arguments]
copyright_check +files:
    #!/bin/sh -e
    exit_code=0
    for file; do
      if [ -e "$file.license" ]; then
        if [ ! -e "$file" ]; then
          printf 'Leftover license file for: %s\n' "$file" >&2
          exit_code=1
          continue
        fi
        to_check="$file.license";
      else
        if [ ! -e "$file" ]; then continue; fi
        to_check="$file";
      fi
      if ! grep -q "$(date '+SPDX-FileCopyrightText:.*%Y')" "$to_check"; then
        printf '%s needs its copyright updated.\n' "$file" >&2
        exit_code=1
      fi
    done
    exit "$exit_code"

[doc("Validate C formatting with `clang-format-19`")]
[group("lints")]
clang-fmt-check +files:
    clang-format-19 -n -Werror {{ files }}



[group("lints")]
[doc("run all lints other than copyright_check on all `files`")]
timeless-lints +files: runmatch reuse
    tools/runmatch '*.[ch]' just --no-deps clang-fmt-check '{-}' {{ files }}
    tools/runmatch '*.c' just --no-deps cppcheck-single '{-}' {{ files }}
    tools/runmatch '*.sh' checkbashims -f '{-}' {{ files }}
    tools/runmatch '*.sh' shellcheck '{-}' {{ files }}
    codespell {{ files }}

# TOOLS - compiled tools needed for other jobs

[doc("Compile the `tools/runmatch` helper binary")]
[group("tools")]
[working-directory('./tools')]
runmatch:
    make CC=gcc CFLAGS={{quote(gcc_strict_flags)}} runmatch

[doc("Compile the `tests/test_driver` binary")]
[group("tools")]
[working-directory('./tests')]
test_driver:
    make CC=gcc CFLAGS={{quote(gcc_ubsan_flags)}} test_driver

[doc("Build `unit_test_driver` binary")]
[group("tools")]
unit_test_driver:
    make CFLAGS={{ quote(gcc_ubsan_flags) }} unit_test_driver



# META - just what it looks like

[group("meta")]
[doc("build all tools")]
all-tools: unit_test_driver runmatch test_driver

[group("meta")]
[doc("run all tests")]
all-tests: \
    strict-gcc cppcheck-full scan-build test_driver alt-builds-dir unit-test
    env BFC_DONT_SKIP_TESTS=1 just --no-deps ubsan-test int-torture-test

[group("meta")]
[doc("run all applicable lints on **files**")]
all-lints +files: (copyright_check files) (timeless-lints files)

# PRIVATE - used to help implement other rules

[private]
@pre-tarball-checks: git-clean-check
    make clean eambfc
    ./eambfc -V | grep -q "$(date +'Copyright (c) .*%Y')"
    git ls-files -z --exclude-standard -- ':(attr:!no-tar)' |\
        xargs -0 just timeless-lints version.h

[private]
[working-directory('./tools/execfmt_support')]
can_run arch:
    @'./{{ arch }}'
    @echo {{ "Can run " + arch + "Linux ELF binaries" }}

[private]
can_run_all:
    @for arch in {{ backends }}; do just --no-deps can_run "$arch"; done

[private]
alt-builds-dir:
    mkdir -p alt-builds

[private]
git-clean-check:
    #!/bin/sh
    if [ -n "$(git status --short)" ]; then
        printf 'Uncommitted changes!\n' >&2
        if [ -z "$ALLOW_UNCOMMITTED" ]; then
            printf 'IF TESTING: ' >&2
            printf 'To forcibly proceed, rerun with the ALLOW_UNCOMMITTED ' >&2
            printf 'environment variable set to a non-empty value.\n' >&2
            exit 1
        fi
    fi


[private]
test_build cc *cflags: tarball
    #!/bin/sh
    set -e
    build_dir="$(mktemp -d "/tmp/eambfc-test_build-XXXXXXXXXX")"
    cd "$build_dir"
    tar --strip-components=1 -xf {{quote(src_tarball_path)}}
    make CC={{cc}} CFLAGS={{quote(cflags)}} eambfc test
    cd {{justfile_dir()}}
    rm -rf "$build_dir"

[private]
valgrind_test cflags='-O2 -g3': tarball
    #!/bin/sh
    set -e
    build_dir="$(mktemp -d "/tmp/eambfc-valgrind_test-XXXXXXXXXX")"
    cd "$build_dir"
    tar --strip-components=1 -xf {{quote(src_tarball_path)}}
    make CC=gcc CFLAGS='{{cflags}}' eambfc unit_test_driver
    # there's a 40-byte originating from a library used by libLLVM that I can't
    # figure out how to avoid, so suppress it
    valgrind --error-exitcode=1 --track-fds=yes -q \
        --suppressions={{quote(justfile_dir() / "tools/unit_test.supp")}} \
        ./unit_test_driver
    cd tests
    make CC=gcc CFLAGS='{{cflags}}' test_driver
    valgrind --trace-children=yes \
        --trace-children-skip='../tools/execfmt_support/*,./*' \
        --error-exitcode=1 --exit-on-first-error=yes \
        --track-fds=yes -q ./test_driver
    cd {{ quote(justfile_dir()) }}
    rm -rf "$build_dir"

[private]
pdpmake: tarball
    #!/bin/sh
    set -e
    build_dir="$(mktemp -d "/tmp/eambfc-pdpmake-XXXXXXXXXX")"
    cd "$build_dir"
    tar --strip-components=1 -xf {{quote(src_tarball_path)}}
    mkdir bin
    cd bin
    ln -s {{require('pdpmake')}} make
    cd ..
    PATH="$build_dir/bin:$PATH"
    make clean test unit_test_driver
    cd {{ quote(justfile_dir()) }}
    rm -rf "$build_dir"
