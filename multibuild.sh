#!/bin/sh

# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only
#
set -e
cd "$(dirname "$0")"
[ -d alt-builds ] || mkdir alt-builds
# during setup, errors are fatal. Setup is over, errors are no longer fatal.
set +e

total=0
success=0
failed=0
skipped=0

build_with () {
    build_id="$(printf '%s\0' "$@" | cksum | cut -d ' ' -f 1)"
    cc="$1"
    shift
    build_name="alt-builds/eambfc-$build_id"
    if command -v "$cc" >/dev/null; then
        printf 'running %s with arguments [%s %s].\n' "$cc" "$*" \
            '-D _POSIX_C_SOURCE=200809L'
        total=$((total+1))
        if "$cc" "$@" -D _POSIX_C_SOURCE=200809L \
            serialize.c eam_compile.c json_escape.c main.c \
            -o "$build_name"; then
            eambfc_cmd="$build_name"
            if [ -z "$SKIP_TEST" ]; then
                if (cd tests && make -s EAMBFC="../$eambfc_cmd" test) \
                    >"alt-builds/$build_id.out"; then
                    success=$((success+1))
                else
                    failed=$((failed+1))
                    return 1
                fi
            else
                success=$((success+1))
            fi
        else
            failed=$((failed+1))
            return 1
        fi
    else
        printf 'compiler %s not found, skipping.\n' "$cc"
        skipped=$((skipped+1))
    fi
}

gcc_clang_args='-Wall -Werror -Wextra -pedantic -std=c99'
# shellcheck disable=2086 # word splitting is intentional here
build_with gcc $gcc_clang_args
# shellcheck disable=2086 # word splitting is intentional here
build_with musl-gcc $gcc_clang_args
# shellcheck disable=2086 # word splitting is intentional here
build_with clang $gcc_clang_args

# a bunch of prerequisites for testing on non-s390x systems - compiler must be
# installed, and binfmt support must be enabled and provided for s390x ELF files
# by qemu-s390x.
if [ "$(uname -m)" = s390x ] || { [ "$(uname)" = Linux ] &&
    [ "$(cat /proc/sys/fs/binfmt_misc/status)" = 'enabled' ] &&
    [ "$(head -n1 /proc/sys/fs/binfmt_misc/qemu-s390x)" = 'enabled' ]; } ; then
    # shellcheck disable=2086 # word splitting is intentional here
    build_with s390x-linux-gnu-gcc $gcc_clang_args -static
else
    skipped=$((skipped+1))
fi

build_with tcc
# shellcheck disable=2086 # word splitting is intentional here
build_with zig cc $gcc_clang_args # zig's built-in C compiler

printf '\n\n################################\n'
printf 'TOTAL COMPILERS TESTED: %d\n' "$total"
printf 'TOTAL COMPILERS SKIPPED: %d\n' "$skipped"
printf 'TOTAL COMPILERS SUCCESSFUL: %d\n' "$success"
printf 'TOTAL COMPILERS FAILED: %d\n' "$failed"
[ "$failed" -eq 0 ]
