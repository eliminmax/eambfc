#!/bin/sh

# SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

# this script is used to generate release builds and source tarballs
# it is not written with portability in mind, in any way, shape, or form.

# Dependencies:
# this script uses commands from the following Debian 12 (Bookworm) packages:
#
# * binutils
# * coreutils
# * git
# * gzip
# * make
# * parallel
# * sed
# * tar
# * valgrind
# * xz-utils
# * clang-tools-19
#
# The static analyses and linting done with run-lints.sh depend on the following
# additional packages:
#
# * clang-format-19
# * codespell
# * devscripts
# * findutils
# * shellcheck
#
# Additionally, the main Makefile, test Makefile, and scripts they call use
# commands and libraries from the following packages and their dependencies:
#
# * gcc
# * gcc-i686-linux-gnu
# * gcc-s390x-linux-gnu
# * gcc-mips-linux-gnu
# * gcc-aarch64-linux-gnu
# * musl-tools
# * gawk
# * clang-19 (with /usr/lib/llvm-19/bin in $PATH, so it's invoked as clang)
# * libcunit1-dev
# * libubsan1
# * libasan6
# * llvm-19-dev
# * tcc
# * qemu-user-binfmt (backport version, as stable segfaults sometimes for s390x)
#
# Lastly, a few tools not packaged for Debian are required - here's a list, with
# URLS and info on how I installed them.
#
# the Zig compiler's built-in C compiler
#       https://ziglang.org/
#   I downloaded the binary and symlinked it into the PATH
#
# Ron Yorston's Public Domain POSIX make implementation - installed as pdpmake
#       https://frippery.org/make/
#   I downloaded source, built by running GNU make, then copied into PATH
#
# reuse helper tool >= 5.0.0 (newer than Debian package in bookworm/main)
#       https://git.fsfe.org/reuse/tool
#   I installed with pipx, which was in turn installed with apt
#
# cppcheck >= 2.16.0 (newer than Debian package in Bookworm/main
#       https://github.com/danmar/cppcheck
#   I built with cmake, installed into its own prefix, then symlinked it into
#   PATH - finds more issues than the older version bundled by Debian

set -e

cd "$(dirname "$(realpath "$0")")"

if [ -z "$FORCE_RELEASE" ] && [ -n "$(git status --short)" ]; then
    printf 'Will not build source tarball with uncommitted changes!\n' >&2
    printf 'If you want to test changes to this script, set the ' >&2
    printf 'FORCE_RELEASE environment variable to a non-empty value.\n' >&2
    exit 1
fi

# make make shut up
export MAKEFLAGS=-s

make clean
# generate version.h
./gen_version_h.sh

# make sure version output copyright info includes the current year
make eambfc
./eambfc -V | grep -q "$(date +'Copyright (c) .*%Y')"
make clean

# first, some linting - bypass copyright check as older files are still checked
# even if not changed this year
ALLOW_SUSPECT_COPYRIGHTS=y ./run-lints.sh ./*.[ch] ./*.sh .githooks/*

# run codespell on files not included in those globs
codespell --skip='.git','*.sh','.githooks','*.[ch]'
# check for unused functions - this was not done with run-lints.sh, as it lacks
# context if not looking at every file
cppcheck -q --std=c99 -D__GNUC__ --enable=unusedFunction \
    --error-exitcode=1 --suppress=checkersReport --check-level=exhaustive \
    -DBFC_NOATTRIBUTES ./*.c

# use scan-build to test for potential issues
scan-build-19 --status-bugs make CFLAGS=-O2 && make clean

version="$(cat version)"

build_name="eambfc-$(git describe --tags)"
if [ -n "$(git status --short)" ]; then build_name="$build_name-localchg"; fi

src_tarball_name="$build_name-src.tar"

mkdir -p releases/
# remove existing build artifacts with the current build name
rm -rf releases/"$build_name"*

# change the git commit in version.h to reflect that it's a source tarball build
sed '/git commit: /s/"/"source tarball from /' -i version.h

git ls-files -coz --exclude-standard ':!:.githooks'':!:*/.gitignore' | \
    xargs -0 tar --xform="s#^#$build_name-src/#" \
        -cf "releases/$src_tarball_name" version.h

gzip -9 -k "releases/$src_tarball_name"
xz -9 -k "releases/$src_tarball_name"

build_dir="$(mktemp -d "/tmp/eambfc-$version-build-XXXXXXXXXX")"
cp releases/"$src_tarball_name" "$build_dir"

old_pwd="$PWD"
cd "$build_dir"

tar --strip-components=1 -xf "$src_tarball_name"

# copy version.h so that it's not overwritten before the real build
cp version.h version.h-real

make CC=gcc all_tests

# ensure strict and ubsan builds work at all gcc optimization levels, and
# valgrind finds no issues
for o_lvl in 0 1 2 3 s fast g z; do
    make CC=gcc CFLAGS="-O$o_lvl" clean ubsan strict int_torture_test
    SKIP_DEAD_CODE=1 make \
        EAMBFC_ARGS=-k EAMBFC=../alt-builds/eambfc-ubsan all_arch_test
done

printf '%s\n' 0 1 2 3 s fast g z | \
    EAMBFC_VALGRIND=1 parallel -I_olvl ./tools/test-build.sh \
        "$src_tarball_name" gcc -O_olvl

# try with a mix of compilers
for o_lvl in 0 1 2 3; do
    printf 'gcc\nmusl-gcc\nclang\nzig cc\n' |\
        parallel -I_cc ./tools/test-build.sh "$src_tarball_name" \
            _cc "-O$o_lvl" -Wall -Werror -Wextra -pedantic -std=c99
done

# build for specific architectures now - the four were chosen to catch any bugs
# that are specific to 32-bit or 64-bit systems, and/or endianness-specific.
for o_lvl in 0 1 2 3; do
    printf '%s-linux-gnu-gcc\n' i686 aarch64 mips s390x |\
        LDFLAGS=-static parallel -I_cc ./tools/test-build.sh \
            "$src_tarball_name" \
            _cc "-O$o_lvl" -Wall -Werror -Wextra -pedantic -std=c99 -static
done

# this has caught loss of *const qualifier that the big league compilers did not
./tools/test-build.sh "$src_tarball_name" \
    tcc -Wall -Wwrite-strings -Wunsupported -Werror

# test GNU longopts builds
for olvl in 0 1 2 3; do
    make CFLAGS="-D_GNU_SOURCE -DBFC_LONGOPTS=1 -O$olvl" clean strict ubsan
done

# portability test - ensure a minimal, public domain POSIX make implementation
# can complete the clean, eambfc, and test targets

# ensure that recursive calls use the right make by putting it first in $PATH
make -s clean
mkdir -p .utils
ln -sfv "$(command -v pdpmake)" .utils/make
env PATH="$PWD/.utils:$PATH" make clean eambfc test

# if none of the previous tests failed, it's time for the actual build
make -s clean
mv version.h-real version.h
make CC=gcc CFLAGS="-O2 -std=c99 -flto" LDFLAGS="-flto"
strip eambfc
mv eambfc "$old_pwd/releases/eambfc-$version"
cd "$old_pwd"
rm -rf "$build_dir"
