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

src_files='serialize.c compile.c x86_64_encoders.c err.c util.c optimize.c'
src_files="$src_files main.c"

posix_flag='-D _POSIX_C_SOURCE=200809L'

build_with () {
    build_id="$(printf '%s' "$@" | tr -cd 'A-Za-z0-9_+-')"
    cc="$1"
    shift
    build_name="alt-builds/eambfc-$build_id"
    if command -v "$cc" >/dev/null; then
        # split command in 3 to not have an extra space if no args were passed
        printf 'running %s with arguments [' "$cc"
        printf '%s ' "$@"
        printf '%s].\n' "$posix_flag"
        total=$((total+1))
        # shellcheck disable=SC2086 # word splitting is intentional here
        if "$cc" "$@" $posix_flag $src_files -o "$build_name"; then
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
# test at different optimization levels
for op_lv in 0 1 2 3; do
    # shellcheck disable=2086 # word splitting is intentional here
    build_with gcc $gcc_clang_args -O"$op_lv"
    # shellcheck disable=2086 # word splitting is intentional here
    build_with musl-gcc $gcc_clang_args -O"$op_lv"
    # shellcheck disable=2086 # word splitting is intentional here
    build_with clang $gcc_clang_args -O"$op_lv"
    # shellcheck disable=2086 # word splitting is intentional here
    build_with zig cc $gcc_clang_args -O"$op_lv" # zig's built-in C compiler

    # a bunch of prerequisites for testing on non-s390x systems - qemu-user must
    # be installed, and binfmt support must be enabled and provided for s390x
    # ELF executables by qemu-s390x.
    if [ "$(uname -m)" != s390x ] && [ "$(uname)" = Linux ] &&\
        [ "$(cat /proc/sys/fs/binfmt_misc/status)" = 'enabled' ] &&\
        [ "$(head -n1 /proc/sys/fs/binfmt_misc/qemu-s390x)" = 'enabled' ]; then
        # shellcheck disable=2086 # word splitting is intentional here
        build_with s390x-linux-gnu-gcc $gcc_clang_args -O"$op_lv" -static
    else
        skipped=$((skipped+1))
    fi
done

build_with tcc

printf '\n\n################################\n'
printf 'TOTAL COMPILERS TESTED: %d\n' "$total"
printf 'TOTAL COMPILERS SKIPPED: %d\n' "$skipped"
printf 'TOTAL COMPILERS SUCCESSFUL: %d\n' "$success"
printf 'TOTAL COMPILERS FAILED: %d\n' "$failed"

# exit code set by this last line
# shellcheck disable=2015 # different semantics than if-then-else are intended.
[ "$failed" -eq 0 ] && [ -z "$NO_SKIP_MULTIBUILD" ] || [ "$skipped" -eq 0 ]
