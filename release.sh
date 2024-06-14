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

gcc_warnings='-O2 -Wall -Wextra -Wno-error=inline -Werror -Wpedantic '\
'-Winline -Wformat-security -Wformat-signedness -Wduplicated-branches '\
'-Wduplicated-cond -Wbad-function-cast -Waggregate-return '\
'-Wdate-time -Winit-self -Wundef -Wexpansion-to-defined -Wunused-macros '\
'-Wnull-dereference -Wsuggest-attribute=const'

# test has eambfc as a prerequisite, and tests should pass.
make CC=gcc CFLAGS="-flto -std=c99 -O2 $gcc_warnings" LDFLAGS="-flto" test
mv eambfc "$old_pwd/releases/eambfc-$version"
cd "$old_pwd"
rm -rf "$build_dir"

xz -9 -k releases/eambfc-"$version"
gzip -9 -k releases/eambfc-"$version"
