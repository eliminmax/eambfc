#!/usr/bin/env -S just -f

# SPDX-FileCopyrightText: 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

# __BACKENDS__: add backend here
backends := 'arm64 riscv64 s390x x86_64'

version_string := (
    trim(read("version")) +
    "-git-" +
    trim(shell("git describe --tags")) +
    replace_regex(shell("git status --short | tr -d '\n'"), ".+", "-localchg")
)
backend_sources := replace_regex(backends, '\b([^ ]+)\b', 'backend_${1}.c')

unibuild_files := (
    'serialize.c compile.c optimize.c ' + backend_sources +
    ' err.c util.c resource_mgr.c parse_args.c main.c '
)

# aligning it like this was not easy, but it sure is satisfying
gcc_strict_flags := (
    '-std=c99 -D_POSIX_C_SOURCE=200908L -O3 -fanalyzer -Wall -Wextra -Werror ' +
    '-Wpedantic -Wformat-truncation=2 -Wduplicated-branches -Wshadow -Wundef ' +
    '-Wformat-overflow=2 -Wformat-signedness -Wbad-function-cast -Winit-self ' +
    '-Wnull-dereference -Wredundant-decls -Wduplicated-cond -Warray-bounds=2 ' +
    '-Wuninitialized -Wlogical-op -Wwrite-strings -Wformat=2 -Wunused-macros ' +
    '-Wcast-align=strict -Wcast-qual -Wtrampolines'
)

gcc_ubsan_flags := gcc_strict_flags + ' ' + (
    '-fsanitize=address,undefined -fno-sanitize-recover=all'
)



# GENERAL

[doc("Build the basic `eambfc` binary with `make`")]
[group("general")]
eambfc:
    make eambfc

[doc("Create a release build of `eambfc`")]
[group("general")]
eambfc-release:
    # TODO: migrate this into justfile
    ./release.sh



# TESTS - static analysis, unit tests, cli tests, etc.

[doc("Run `eambfc` through GCC's static analyzer")]
[group("tests")]
strict-gcc:
    gcc {{ gcc_strict_flags }} {{ unibuild_files }} -o /dev/null

[doc("Run the full project through the `cppcheck` static analyzer")]
[group("tests")]
cppcheck-full:
    cppcheck -q --std=c99 -D__GNUC__ --error-exitcode=1 \
        --check-level=exhaustive --enable=all --disable=missingInclude \
        --suppress=checkersReport {{ unibuild_files }}

[doc("Run `eambfc` through LLVM's `scan-build` static analyzer")]
[group("tests")]
scan-build:
    scan-build-19 --status-bugs make CFLAGS=-O3 CC=clang-19 clean eambfc
    make clean

[doc("Build `eambfc` with **UBsan**, and run through the test suite")]
[group("tests")]
ubsan-test: alt-builds-dir
    gcc  {{ gcc_ubsan_flags }} {{ unibuild_files }} -o alt-builds/eambfc-ubsan
    just test alt-builds/eambfc-ubsan

[doc("Build and test `eambfc` with 64-bit integer fallback hackery")]
[group("tests")]
int-torture-test: alt-builds-dir test_driver
    gcc -D INT_TORTURE_TEST=1 {{gcc_ubsan_flags}} -Wno-pedantic \
        {{ unibuild_files }} -o alt-builds/eambfc-itt
    just test alt-builds/eambfc-itt

[doc("Test provided `eambfc` build using its cli")]
[group("tests")]
[working-directory('./tests')]
test eambfc="eambfc": test_driver
    ./test_driver {{ join(invocation_dir(), eambfc) }}

[doc("Run through the unit tests")]
[group("tests")]
unit-test: unit_test_driver
    ./unit_test_driver



# LINTS - check style and quality of individual files

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
all-tests:
    just strict-gcc
    just cppcheck-full
    just scan-build
    env BFC_DONT_SKIP_TESTS=1 just ubsan-test
    env BFC_DONT_SKIP_TESTS=1 just int-torture-test
    env BFC_DONT_SKIP_TESTS=1 just unit-test



# PRIVATE

[private]
[working-directory('./tools/execfmt_support')]
can_run arch:
    @'./{{ arch }}'
    @echo {{ "Can run " + arch + "Linux ELF binaries" }}

[private]
can_run_all:
    @for arch in {{ backends }}; do just can_run "$arch"; done

[private]
@pre_commit_checks +files: runmatch
    just copyright_check {{ files }}
    tools/runmatch '*.[ch]' just clang-fmt-check '{-}' {{ files }}
    tools/runmatch '*.c' just cppcheck-single '{-}' {{ files }}
    codespell {{ files }}

[private]
alt-builds-dir:
    mkdir -p alt-builds
