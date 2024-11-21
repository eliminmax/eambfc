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
# * cppcheck
# * devscripts
# * findutils
# * shellcheck
#
# Lastly, a few tools not packaged for Debian are required - here's a list, with
# URLS and info on how I installed them.
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

# check formatting of C source and header files
find . -name '*.[ch]' -type f -exec clang-format-16 -n -Werror {} +

# run checks on shell files with checkbashisms and shellcheck
find . -name '*.sh' -type f -exec shellcheck {} + -exec checkbashisms -f {} +

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
cppcheck -q --enable=all --disable=missingInclude --std=c99 --error-exitcode=2 .

# Find typos in the code
# Learned about this tool from Lasse Colin's writeup of the xz backdoor. Really.
codespell --skip=.git
