#!/bin/sh -e

# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

cd "$(dirname "$0")"


if [ ! -n "$FORCE_RELEASE" ] && [ -n "$(git status --short)" ]; then
    printf 'Will not build source tarball with uncommitted changes!\n' >&2
    exit 1
fi

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


# stop here on unsupported systems
if [ "$(uname -mo)" != 'x86_64 GNU/Linux' ]; then
    printf 'Not building for non-x86_64 systems.\n'
    exit 0
fi

build_dir="$(mktemp -d "/tmp/eambfc-$version-build-XXXXXXXXXX")"
cp releases/"eambfc-$version-src.tar" "$build_dir"

old_pwd="$PWD"
cd "$build_dir"
tar --strip-components=1 -xf "eambfc-$version-src.tar"
# multibuild.sh fails if any compilers are skipped
NO_SKIP_MULTIBUILD=yep make all_tests

make CC=gcc CFLAGS="-O2 -std=c99 -flto" LDFLAGS="-flto"
mv eambfc "$old_pwd/releases/eambfc-$version"
cd "$old_pwd"
rm -rf "$build_dir"

xz -9 -k releases/eambfc-"$version"
gzip -9 -k releases/eambfc-"$version"
