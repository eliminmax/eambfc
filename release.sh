#!/bin/sh -e

# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

# this script is used to generate release builds and source tarballs
# it is not written with portability in mind.
# Requires Git, xz, gzip, several GNU-isms, and QEMU binfmt support,
# and the following C compilers:
# - gcc
# - s390x-linux-gnu-gcc
# - tcc
# - musl-gcc
# - clang
# - zig cc
#
# the following linting tools are needed:
# - codespell
# - shellcheck
#
# all of the dependencies except for zig are available in the Debian repos

cd "$(dirname "$(realpath "$0")")"


if [ ! -n "$FORCE_RELEASE" ] && [ -n "$(git status --short)" ]; then
    printf 'Will not build source tarball with uncommitted changes!\n' >&2
    printf 'If you want to test changes to this script, set the ' >&2
    printf 'FORCE_RELEASE environment variable to a non-empty value.\n' >&2
    exit 1
fi

# first, some linting
# Catch typos in the code.
# Learned about this one from Lasse Colin's writeup of the xz backdoor. Really.
codespell --skip=.git

find . -name '*.sh' -type f -exec shellcheck --norc {} \+

version="$(cat version)-$(
    git log -n 1 --date=format:'%Y-%m-%d' --pretty=format:'%ad-%h'
)"

build_name="eambfc-$version"
src_tarball_name="$build_name-src.tar"
mkdir -p releases/
rm -rf releases/"$build_name"*

# generate config.h
make -s clean config.h

# change the git commit in config.h
sed '/git commit: /s/"/"source tarball from /' -i config.h

git archive HEAD --format=tar      \
    --prefix="$build_name"/        \
    --add-file=config.h            \
    --output=releases/"$src_tarball_name"

gzip -9 -k "releases/$src_tarball_name"
xz -9 -k "releases/$src_tarball_name"


build_dir="$(mktemp -d "/tmp/eambfc-$version-build-XXXXXXXXXX")"
cp releases/"eambfc-$version-src.tar" "$build_dir"

old_pwd="$PWD"
cd "$build_dir"
tar --strip-components=1 -xf "eambfc-$version-src.tar"
# multibuild.sh fails if any compilers are skipped and env var is non-empty
NO_SKIP_MULTIBUILD=yep make CC=gcc all_tests

make clean

make CC=gcc CFLAGS="-O2 -std=c99 -flto" LDFLAGS="-flto"
mv eambfc "$old_pwd/releases/eambfc-$version"
cd "$old_pwd"
rm -rf "$build_dir"

xz -9 -k releases/eambfc-"$version"
gzip -9 -k releases/eambfc-"$version"
