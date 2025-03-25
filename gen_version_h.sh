#!/bin/sh

# SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

git_str="$(git log -n1 --pretty=format:'git commit: %h')";
if [ -n "$(git status --short)" ]; then
    git_str="$git_str (with local changes)";
fi

cat >version.h <<EOF
/* SPDX-FileCopyrightText: NONE
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * generated header file with version information */
#ifndef BFC_VERSION_H
#define BFC_VERSION_H 1

#define BFC_VERSION "$(cat version)"

#define BFC_COMMIT "$git_str"

#endif /* BFC_VERSION_H */
EOF
