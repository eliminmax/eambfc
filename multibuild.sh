#!/bin/sh

# SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
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

interface_files='backend_arm64.c backend_x86_64.c backend_s390x.c'
misc_src_files='serialize.c compile.c err.c util.c optimize.c resource_mgr.c'
src_files="$interface_files $misc_src_files main.c"
unset interface_files misc_src_files

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

gcc_clang_test() {
    for op_lv in 0 1 2 3; do
        # shellcheck disable=SC2086 # word splitting is intentional here
        build_with "$@" -Wall -Werror -Wextra -pedantic -std=c99 -O"$op_lv"
    done
}

test_for_triple() {
    triple_gcc="$1-$2-$3-gcc"
    if [ -n "$SKIP_TEST" ]; then
        gcc_clang_test "$triple_gcc" -static
    elif command -v "$triple_gcc" >/dev/null 2>&1 && {
        printf 'int main(void){return 0;}' | "$triple_gcc" -x c -static -
        ./a.out  >/dev/null 2>&1
        rm a.out
    }; then
        gcc_clang_test "$triple_gcc" -static
    else
        printf 'Either don'\''t have gcc for triple %s-%s-%s, ' "$1" "$2" "$3"
        printf 'or have it but can'\''t run the output properly, so skipping.\n'
        printf '\t(skipping tests at 4 optimization levels.)\n'
        skipped=$((skipped+4))
        total=$((total+4))
    fi
}

gcc_clang_test gcc
gcc_clang_test musl-gcc
gcc_clang_test clang
gcc_clang_test zig cc

test_for_triple i686 linux gnu # 32-bit little-endian
test_for_triple aarch64 linux gnu # 64-bit little-endian
test_for_triple mips linux gnu # 32-bit big-endian
test_for_triple s390x linux gnu # 64-bit big-endian

build_with tcc

printf '\n\n################################\n'
printf 'TOTAL COMPILERS TESTED: %d\n' "$total"
printf 'TOTAL COMPILERS SKIPPED: %d\n' "$skipped"
printf 'TOTAL COMPILERS SUCCESSFUL: %d\n' "$success"
printf 'TOTAL COMPILERS FAILED: %d\n' "$failed"

# exit code is set by this last line
[ "$failed" -eq 0 ] && { [ -z "$NO_SKIP_MULTIBUILD" ] || [ "$skipped" -eq 0 ]; }
