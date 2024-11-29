#!/bin/sh

# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

# This script runs various light linting and static analysis tools to try to
# ensure a halfway-decent quality for the codebase

# Dependencies:
# this script uses commands from the following Debian 12 (Bookworm) packages:
# * clang-format-16
# * codespell
# * coreutils
# * devscripts
# * findutils
# * shellcheck
#
# Additionally, a few tools not packaged for Debian are required - here's a list
# with URLS and info on how I installed them.
#
# Ron Yorston's Public Domain POSIX make implementation - installed as pdpmake
#       https://frippery.org/make/
#   I downloaded source, built by running GNU make, then copied into PATH
#
# reuse helper tool >= 5.0.0 (newer than Debian package in Bookworm/main)
#       https://git.fsfe.org/reuse/tool
#   I installed with pipx, which was in turn installed with apt
#
# cppcheck >= 2.16.0 (newer than Debian package in Bookworm/main
#       https://github.com/danmar/cppcheck
#   I built with cmake, installed into its own prefix, then symlinked it into
#   PATH - finds more issues than the older version bundled by Debian


set -e
cd "$(dirname "$(realpath "$0")")"

# ensure licensing information is structured in a manner that complies with the
# REUSE 3.3 specification
# only want output if there's an issue, so run with -q, and if there's an issue,
# then run again with output.
reuse lint -q || reuse lint

# validate that a POSIX-compliant make can parse the Makefile properly
pdpmake -n clean all test multibuild >/dev/null

for file; do
    if head -c128 "$file" | grep -q '^#! */bin/sh'; then
        shellcheck "$file"
        checkbashisms -f "$file"
    else case "$file" in *.[ch])

        # check that C source and header files meet proper style guides
        clang-format-16 -n -Werror "$file"

        # invoke the cppcheck static analysis tool
        cppcheck -q --std=c99 --platform=unspecified --enable=all \
            --disable=missingInclude,unusedFunction \
            --suppress=checkersReport --suppress=unusedStructMember \
            --check-level=exhaustive --error-exitcode=1 "$file"
        ;;
    esac; fi
    # Find typos in the code
    # Learned about this tool from Lasse Colin's writeup of the xz backdoor.
    codespell "$file"
done

