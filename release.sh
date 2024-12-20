#!/bin/sh

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
# * valgrind
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
# qemu-user-static (backport version, as stable segfaults sometimes for s390x)
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
# reuse helper tool >= 4.0.0 (newer than Debian package in bookworm/main)
#       https://git.fsfe.org/reuse/tool
#   I installed with pipx, which was in turn installed with apt

set -e

cd "$(dirname "$(realpath "$0")")"

if [ -z "$FORCE_RELEASE" ] && [ -n "$(git status --short)" ]; then
    printf 'Will not build source tarball with uncommitted changes!\n' >&2
    printf 'If you want to test changes to this script, set the ' >&2
    printf 'FORCE_RELEASE environment variable to a non-empty value.\n' >&2
    exit 1
fi

make clean

# first, some linting

# Catch typos in the code.
# Learned about this one from Lasse Colin's writeup of the xz backdoor. Really.
codespell --skip=.git

# run shellcheck and checkbashisms on all shell files
find . -name '*.sh' -type f \
    -exec shellcheck {} +\
    -exec checkbashisms -f {} +

# ensure licensing information is structured in a manner that complies with the
# REUSE 3.2 specification
# if reuse lint -q fails, it is silent, so run it again without passing -q to
# suppress output if that happens
reuse lint -q || reuse lint


version="$(cat version)"

build_name="eambfc-$version"
src_tarball_name="$build_name-src.tar"
mkdir -p releases/
# remove existing
rm -rf releases/"$build_name"*

# generate config.h
make -s clean config.h

# change the git commit in config.h to reflect that it's a source tarball build
sed '/git commit: /s/"/"source tarball from /' -i config.h

git archive HEAD --format=tar      \
    --prefix="$build_name-src"/    \
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

# generate a wrapper script to run test suite under valgrind's watchful eye.
# shellcheck disable=SC2016 # this shouldn't be expanded here
printf > valgrind-eambfc.sh '#!/bin/sh
exec valgrind "$(dirname "$(realpath "$0")")"/eambfc "$@"\n'
chmod +x valgrind-eambfc.sh

test_for_arch() {
    # run test suite with and without EAMBFC optimization mode with ubsan
    # to try to catch undefined behavior
    make EAMBFC=../alt-builds/eambfc-ubsan EAMBFC_ARGS="-kOa$1" clean test
    SKIP_DEAD_CODE=1 \
        make EAMBFC=../alt-builds/eambfc-ubsan EAMBFC_ARGS="-ka$1" clean test
    # do the same with the valgrind wrapper script
    make EAMBFC=../valgrind-eambfc.sh EAMBFC_ARGS="-kOa$1" clean test
    SKIP_DEAD_CODE=1 \
        make EAMBFC=../valgrind-eambfc.sh EAMBFC_ARGS="-ka$1" clean test
}

# enable s390x target for testing purposes
sed -i '/TARGET_S390X/s/ 0/ 1/' config.h
# ensure strict and ubsan builds work at all gcc optimization levels
for o_lvl in 0 1 2 3 s fast g z; do
    make CC=gcc CFLAGS="-O$o_lvl" clean ubsan strict eambfc;
    cd tests
    # __BACKENDS__
    test_for_arch x86_64
    test_for_arch s390x
    test_for_arch arm64
    cd ..
done
sed -i /TARGET_S390X/s/1/0/ config.h

# portability test - can a minimal, public domain POSIX make implementation
# complete the clean, eambfc, and test targets?
# move config.h out of the way so that it isn't clobbered by pdpmake
mv config.h config.h-real
# ensure that recursive calls use the right make by putting it first in $PATH
mkdir -p .utils
ln -sfv "$(command -v pdpmake)" .utils/make
env PATH="$PWD/.utils:$PATH" make clean eambfc test

make clean
# move config.h back in place for real build
mv config.h-real config.h

# if none of the previous tests failed, it's time for the actual build
make CC=gcc CFLAGS="-O2 -std=c99 -flto" LDFLAGS="-flto"
strip eambfc
mv eambfc "$old_pwd/releases/eambfc-$version"
cd "$old_pwd"
rm -rf "$build_dir"
