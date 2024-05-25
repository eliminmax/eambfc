#!/bin/sh

# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

# exit on error during initial setup
set -eu

cd "$(dirname "$0")"

( cd .. && make -s eambfc minielf )

# if this runs successfully, we can run ELF files properly.
make -s build-all can-run

# now we should be properly set up, and can run tests properly
# don't exit on error anymore
set +e

successes=0
fails=0
total=0

# run a binary that takes no input, and validate that the checksum matches
test_simple () {
    total=$((total+1))
    if [ "$(./"$1" | cksum)" = "$2" ]; then
        successes=$((successes+1))
        printf 'SUCCESS - %s\n' "$1"
    else
        fails=$((fails+1))
        printf 'FAIL - output mismatch: %s\n' "$1"
    fi
}

# a lot like test_simple, but this time, instead of testing the binary, check
# that the error message matches the expectation
test_error () {
    total=$((total+1))
    errfile=".$1.build_err"
    if ! [ -e "$errfile" ]; then
        printf 'FAIL - missing expected build error file: %s\n' "$errfile"
        fails=$((fails+1))
    else
        # TODO: Test for specific error instead of assuming success
        printf 'SUCCESS - hit expected compilation error for %s\n' "$1"
        successes=$((successes+1))
    fi
}

test_simple colortest '1395950558 3437'
test_simple hello '1639980005 14'
test_simple loop '159651250 1'
test_simple null '4294967295 0'
test_simple wrap '781852651 4'
test_simple wrap2 '1742477431 4'

test_error too-many-nested-loops


# lastly, some special cases

# ensure that every possible character is handled properly by rw
# octal is portable across all POSIX-compliant printf.1 implementations
# all unsigned 8-bit values in octal are in the range 000 - 377

# create a variable called "bvs" containing every possible byte value in octal,
# encoded for use with printf '%b'
bvs=''
for n0 in 0 1 2 3; do
    for n1 in 0 1 2 3 4 5 6 7; do
        for n2 in 0 1 2 3 4 5 6 7; do
            bvs="$bvs $n0$n1$n2"
        done
    done
done

# pipe through `od` to ensure it can be handled cleanly as text
# and is encoded the same way as it is in $bvs, including the leading space
output_bvs="$(for bv in $bvs; do printf '%b' "\\0$bv" | ./rw; done |\
        od -to1 -An | tr -d '\n')"

total=$((total+1))
if [ "$bvs" = "$output_bvs" ]; then
    successes=$((successes+1))
    printf 'SUCCESS - rw works for all 8-bit values.\n'
else
    fails=$((fails+1))
    printf 'FAIL - rw does not work for all 8-bit values.\n'
fi

# lastly, the truth machine
# it goes on forever if the input is 1, which means that testing is a problem
# the solution is to only check the first 16 characters, then kill the process
#
# POSIX does not mandate that head support the -c argument to read bytes instead
# of lines, but the following is a POSIX-compliant way to grab the first 16
# bytes of stdin.
get16c () { dd bs=16 count=1 2>/dev/null; }

total=$((total+1))
if [ "$(printf 0 | ./truthmachine)" = 0 ] &&:\
    [ "$(printf 1 | ./truthmachine | get16c)" = '1111111111111111' ]; then
        successes=$((successes+1))
        printf 'SUCCESS - truthmachine works for 0, and seems to work for 1. '
        printf '(Just looked at the first 16 bytes)\n'
else
    fails=$((fails+1))
    printf 'FAIL - truthmachine fails for at least one of its two inputs.\n'
fi

printf '########################\n'
printf 'SUCCESSES: %d\nFAILS: %d\nTOTAL: %d\n' "$successes" "$fails" "$total"

# as the last command, this sets the exit code for the script
[ "$fails" -eq 0 ]
