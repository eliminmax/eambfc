#!/bin/sh

# SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

# This script runs various light linting and static analysis tools to try to
# ensure a halfway-decent quality for the codebase

# Dependencies:
# this script uses commands from the following Debian 12 (Bookworm) packages:
# * clang-format-19
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

sh_lints () {
    shellcheck "$file"
    checkbashisms -f "$file"
}

# Check if copyright info includes current year
# Due to the possibility that it's intentional, allow bypassing this check with
# the ALLOW_SUSPECT_COPYRIGHTS variable
copyright_check () {
    if [ -n "$ALLOW_SUSPECT_COPYRIGHTS" ]; then
        return 0
    fi
    if [ -e "$1.license" ]; then
        to_check="$1.license"
    else
        to_check="$1"
    fi
    if ! grep -q "$(date '+SPDX-FileCopyrightText:.*%Y')" "$to_check"; then
        printf '\033[1;31m%s\033[22m may need copyright updated\n' "$1" >&2
        printf '\033[39;2mSet the ALLOW_SUSPECT_COPYRIGHTS environment ' >&2
        printf 'variable to a non-empty value to bypass this value if ' >&2
        printf 'it'\''s as it should be.\033[m\n' >&2
        return 1
    fi
}



for file; do
    if ! [ -e "$file" ]; then
        if [ -e "$file.license" ]; then
            printf '\033[1;31m%s\033[22 has a leftover REUSE' "$file" >&2
            printf 'license file.\033[m\n' >&2
            exit 1
        fi
        continue
    fi
    copyright_check "$file"
    # Find typos in the code
    # Learned about this tool from Lasse Colin's writeup of the xz backdoor.
    codespell "$file"

    case "$file" in
        # check that C header files have proper formatting
        *.h) clang-format-19 -n -Werror "$file" ;;

        # check that C source files have proper formatting and run some
        # static analysis checks
        *.c)
            clang-format-19 -n -Werror "$file"
            cppcheck -q --std=c99 --platform=unspecified --enable=all \
                --library=.norets.cfg -DBFC_NOATTRIBUTES \
                --disable=missingInclude,unusedFunction --error-exitcode=1 \
                --check-level=exhaustive --suppress=checkersReport "$file"
        ;;

        *.sh) sh_lints ;;

        *) if head -c128 "$file" | grep -q '^#! */bin/sh'; then sh_lints; fi ;;

    esac

done

