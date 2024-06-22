#!/bin/sh -e

# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

# this script is used to generate release builds and source tarballs
# it is not written with portability in mind, in any way, shape, or form.

# Dependencies:
# this script uses commands from the following Debian 12 (Bookworm) packages:
#
# * binutils
# * codespell
# * coreutils
# * devscripts
# * findutils
# * git
# * gzip
# * make
# * sed
# * shellcheck
# * tar
# * xz-utils
#
# Additionally, the main Makefile, test Makefile, and scripts they call use
# commands and libraries from the following packages and their dependencies:
#
# gcc
# gcc-s390x-linux-gnu
# musl-gcc
# gawk
# clang
# libubsan1
# libasan6
# tcc
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
# reuse helper ool >= 3.0.0 (newer than Debian package in bookworm/main)
#       https://git.fsfe.org/reuse/tool
#   I installed with pipx, and installed pipx with apt

cd "$(dirname "$(realpath "$0")")"

if [ -z "$FORCE_RELEASE" ] && [ -n "$(git status --short)" ]; then
    printf 'Will not build source tarball with uncommitted changes!\n' >&2
    printf 'If you want to test changes to this script, set the ' >&2
    printf 'FORCE_RELEASE environment variable to a non-empty value.\n' >&2
    exit 1
fi

# first, some linting
# Catch typos in the code.
# Learned about this one from Lasse Colin's writeup of the xz backdoor. Really.
codespell --skip=.git

# run shellcheck and checkbashisms on all shell files
find . -name '*.sh' -type f \
    -exec shellcheck --norc {} +\
    -exec checkbashisms {} +

# ensure licensing information is structured in a manner that complies with the
# REUSE 3.0 specification
reuse lint -q

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

# ensure strict and ubsan builds work at all optimization levels
for o_lvl in 0 1 2 3; do make -s CFLAGS="-O$o_lvl" clean ubsan strict; done

# portability test - can a minimal, public domain POSIX make implementation
# complete the clean, eambfc, and test targets?
mkdir -p .utils
ln -sfv "$(command -v pdpmake)" .utils/make
# include the path for the direct invocation in case shell hashed old location
env PATH="$PWD/.utils/make:$PATH" .utils/make clean eambfc test

make clean

# if none of the previous tests failed, it's time to build the actual build
make CC=gcc CFLAGS="-O2 -std=c99 -flto" LDFLAGS="-flto"
strip eambfc
mv eambfc "$old_pwd/releases/eambfc-$version"
cd "$old_pwd"
rm -rf "$build_dir"
