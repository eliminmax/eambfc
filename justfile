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

# aligning it like this was not easy
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

[doc("Build the basic `eambfc` binary with `make`")]
[group("general")]
eambfc:
    make eambfc

[doc("Build the basic `eambfc` binary with `make`")]
[group("general")]
eambfc-release:
    make clean

[doc("Run `eambfc` through GCC's static analyzer")]
[group("tests")]
strict-gcc:
    gcc {{ gcc_strict_flags }} {{ unibuild_files }} -o /dev/null

[doc("Run `eambfc` through LLVM's `scan-build` static analyzer")]
[group("tests")]
scan-build:
    scan-build-19 --status-bugs make CFLAGS=-O3 CC=clang-19 clean eambfc
    make clean

[doc("Build `eambfc` with **UBsan**, and run through the test suite")]
[group("tests")]
ubsan-test: alt-builds-dir
    gcc  {{ gcc_ubsan_flags }} {{ unibuild_files }} -o alt-builds/eambfc-ubsan
    just all_arch_test alt-builds/eambfc-ubsan

[doc("Build and test `eambfc` with 64-bit integer fallback hackery")]
int-torture-test: alt-builds-dir
    gcc -D INT_TORTURE_TEST=1 {{gcc_ubsan_flags}} -Wno-pedantic \
        {{ unibuild_files }} -o alt-builds/eambfc-itt
    just all_arch_test alt-builds/eambfc-itt

[doc("Test `eambfc` for all targets, with and without `-O`")]
[group("tests")]
[working-directory('./tests')]
all_arch_test eambfc="eambfc": can_run_all
    make EAMBFC={{ join(invocation_dir(), eambfc) }} -s test_all
    SKIP_DEAD_CODE=1 make EAMBFC={{ join(invocation_dir(), eambfc) }} \
        EAMBFC_ARGS=-kj test_all

[doc("Run through the unit tests")]
[group("tests")]
unit-test: unit_test_driver
    ./unit_test_driver

[doc("run cppcheck with options for standalone files")]
[group("lints")]
cppcheck_single +files: argglob
    cppcheck -q --std=c99 --platform=unspecified --enable=all \
      --library=.norets.cfg -DBFC_NOATTRIBUTES \
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

[doc("Compile the `tools/argglob` helper binary")]
[group("tools")]
[working-directory('./tools')]
argglob:
    #!/bin/sh
    if [ ! -e argglob -o "$(stat -c %X argglob)" -lt "$(stat -c %X argglob.c)" ]
    then
      gcc -Wall -Wextra -Werror -pedantic -std=c99 -D_POSIX_C_SOURCE=200908L \
        argglob.c -o argglob
    fi

[doc("Build `unit_test_driver` binary")]
[group("tools")]
unit_test_driver:
    make CFLAGS={{ quote(gcc_ubsan_flags) }} unit_test_driver

[private]
[working-directory('./tools/execfmt_support')]
can_run arch:
    @'./{{ arch }}'
    @echo {{ "Can run " + arch + "Linux ELF binaries" }}

[private]
can_run_all:
    @for arch in {{ backends }}; do just can_run "$arch"; done

[private]
pre_commit_checks +files:
    just copyright_check {{ files }}
    just clang-fmt-check $(tools/argglob '*.[ch]' {{ files }})
    just cppcheck_single $(tools/argglob '*.c' {{ files }})
    codespell {{ files }}

[private]
alt-builds-dir:
    mkdir -p alt-builds
