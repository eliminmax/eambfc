#!/bin/sh -e

# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only


cd "$(dirname "$0")"

mkdir -p releases
version="$(cat version)-$(
    git log -n 1 --date=format:'%Y-%m-%d' --pretty=format:'%ad-%h'
)"
make -s clean config.h
git archive HEAD --format=tar      \
    --prefix=eambfc-"$version"/    \
    --add-file=config.h            \
    --output=releases/eambfc-"$version".tar

xz -9 -k releases/eambfc-"$version".tar
gzip -9 -k releases/eambfc-"$version".tar
