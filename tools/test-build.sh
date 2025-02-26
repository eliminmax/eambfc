#!/bin/sh
# SPDX-FileCopyrightText: 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

set -e

tarball="$(realpath "$1")"
shift
cc="$1"
build_id="$(printf '%s' "$@" | tr -cd 'A-Za-z0-9_+-')"
PS4="<$build_id>: "
shift
set -x

cd "$(dirname "$0")/.."
mkdir -p "alt-builds/$build_id"
cd "alt-builds/$build_id"
tar --strip-components=1 -xf "$tarball"
if [ -n "$EAMBFC_VALGRIND" ]; then
    make -s EAMBFC=../tools/valgrind-eambfc.sh \
        CC="$cc" CFLAGS="$*" all_arch_test
    SKIP_DEAD_CODE=1 make -s EAMBFC=../tools/valgrind-eambfc.sh \
        EAMBFC_ARGS=-k CC="$cc" CFLAGS="$*" all_arch_test
else
    make -s CC="$cc" CFLAGS="$*" all_arch_test
    SKIP_DEAD_CODE=1 make -s EAMBFC_ARGS=-k CC="$cc" CFLAGS="$*" all_arch_test
fi

cd ../..
rm -rf "alt-builds/$build_id"
