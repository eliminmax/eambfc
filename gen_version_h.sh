# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only
if command -v git >/dev/null && [ -e .git ]; then
    git_str="$$(git log -n1 --pretty=format:'git commit: %h')";
    if [ -n "$$(git status --short)" ]; then
        git_str="$$git_str (with local changes)";
    fi
else
    git_str='Not built from git repo';
fi;

cat >version.h <<EOF
/* SPDX-FileCopyrightText NONE
 *
 * SPDX-License-Identifier: 0BSD
 *
 * A generated header file with version information */
#ifndef EAMBFC_VERSION_H
#define EAMBFC_VERSION_H 1

#define EAMBFC_VERSION $(cat version)

#define EAMBFC_COMMIT "$git_str"

#endif /* EAMBFC_VERSION_H */
EOF
