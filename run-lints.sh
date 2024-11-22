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
# * cppcheck (* though I use a newer version *)
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
# reuse helper tool >= 4.0.0 (newer than Debian package in Bookworm/main)
#       https://git.fsfe.org/reuse/tool
#   I installed with pipx, which was in turn installed with apt
#
# Optional: cppcheck >= 2.16.0 (newer than Debian package in Bookworm/main
#       https://github.com/danmar/cppcheck
#   I built with cmake, installed into its own prefix, then symlinked it into
#   PATH - finds more issues than the older version bundled by Debian


set -e
cd "$(dirname "$(realpath "$0")")"

# check formatting of C source and header files
find . -name '*.[ch]' -type f -exec clang-format-16 -n -Werror {} +

# run checks on shell scripts with checkbashisms and shellcheck
find . '(' -name '*.sh' -o -path '.githooks/*' ')' -type f \
    -exec shellcheck {} + -exec checkbashisms -f {} +

# ensure licensing information is structured in a manner that complies with the
# REUSE 3.2 specification
# only want output if there's an issue, so run with -q, and if there's an issue,
# then run again with output.
reuse lint -q || reuse lint

# validate that a POSIX-compliant make can parse the Makefile properly
pdpmake -n clean all test multibuild >/dev/null

# check that C source and header files meet proper style guides
find . -name '*.[ch]' -type f -exec clang-format-16 -n -Werror {} +

# invoke the cppcheck static analysis tool recursively on all C source files
cppck_args='-q --enable=all --disable=missingInclude --std=c99'
new_cppck_args="$cppck_args --check-level=exhaustive"
# if using newer version that supports them, pass extra flags
# this quickly checks if the new flags are supported without running checks
# shellcheck disable=SC2086 # word splitting is intentional here
if cppcheck $new_cppck_args --check-config main.c 2>/dev/null; then
    cppck_args="$new_cppck_args"
fi
# shellcheck disable=SC2086 # word splitting is intentional here
cppcheck $cppck_args --error-exitcode=2 .

# Find typos in the code
# Learned about this tool from Lasse Colin's writeup of the xz backdoor. Really.
codespell --skip=.git
