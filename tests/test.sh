#!/bin/sh
# SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

# exit on error during initial setup
set -e

cd "$(dirname "$0")"

if [ $# -ge 1 ]; then
    EAMBFC="${1}"
    shift
else
    EAMBFC=../eambfc
fi


# now we should be properly set up, and can run tests properly
# don't exit on error anymore
set +e

successes=0
skipped=0
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

errid_pat='s/.*"errorId": *"\([^"]*\).*/\1/'
# a lot like test_simple, but this time, instead of testing the binary, check
# that the error message matches the expectation
test_error () {
    f="$1"
    total=$((total+1))
    errfile=".$1.build_err"
    shift
    if ! [ -e "$errfile" ]; then
        printf 'FAIL - missing expected build error file: %s\n' "$errfile"
        fails=$((fails+1))
    else
        error_codes="$(sed "$errid_pat" "$errfile" |\
        sort -u | xargs)"
        # I'd rather write this hackery instead of requiring a tool like jq or
        # a JSON parsing library for the test suite
        if [ "$error_codes" = "$*" ]; then
            printf 'SUCCESS - hit expected compilation errors for %s\n' "$f"
            successes=$((successes+1))
        else
            printf 'FAIL - mismatched errors for %s\n' "$f"
            fails=$((fails+1))
        fi
    fi
}

# errors thrown when processing arguments
test_arg_error () {
    total=$((total+1))
    expecded_codes="$1"; shift
    cond="$1"; shift
    err_codes="$("$EAMBFC" -j "$@" | sed "$errid_pat")"
    if [ "$err_codes" = "$expecded_codes" ]; then
        printf 'SUCCESS - proper error id when %s.\n' "$cond"
        successes=$((successes+1))
    else
        printf 'FAIL - wrong error id when %s.\n' "$cond"
        fails=$((fails+1))
    fi
}

test_simple colortest '1395950558 3437'
test_simple hello '1639980005 14'
test_simple loop '159651250 1'
test_simple null '4294967295 0'
test_simple wrap '781852651 4'
test_simple wrap2 '1742477431 4'



# identical to the hello world program, but something is different about the
# source file or flags
test_simple hello.elf '1639980005 14' # added extension
test_simple alternative_extension '1639980005 14' # self-explanatory
test_simple unseekable '1639980005 14' # output is a FIFO, can't be seeked
test_simple piped_in '1639980005 14' # input is a FIFO, can't be seeked

# ensure that the proper errors were encountered

# argument processing error
test_arg_error MultipleExtensions 'multiple file extensions' \
    "$@" -e .brf -e .bf hello.bf
test_arg_error MultipleTapeBlockCounts 'multiple tape sizes' \
    "$@" -t 1 -t 32
test_arg_error MissingOperand '-e missing argument' \
    "$@" -e
test_arg_error MissingOperand '-t missing argument' \
    "$@" -t
test_arg_error UnknownArg 'invalid argument provided' \
    "$@" -T
test_arg_error NoSourceFiles 'no source files provided' "$@"
test_arg_error BadSourceExtension 'wrong file extension for source file' \
    "$@" 'test.sh'
test_arg_error TapeSizeZero 'tape size is set to 0 blocks' \
    "$@" -t0 hello.bf
test_arg_error TapeTooLarge 'tape size large enough to cause an overflow' \
    "$@" -t9223372036854775807
test_arg_error MultipleOutputExtensions 'multiple output extensions' \
    "$@" -s .elf -s .out

# some permission issues
chmod 'u-r' 'hello.bf'
test_arg_error OpenReadFailed 'failure to open input file for reading' \
    "$@" 'hello.bf'
chmod 'u+r' 'hello.bf'
touch hello.b
chmod -w hello.b
test_arg_error OpenWriteFailed 'failure to open output file for writing' \
    "$@" -e f hello.bf
rm -f hello.b

# compiler errors
test_error unmatched_close UnmatchedClose
test_error unmatched_open UnmatchedOpen

# lastly, some special cases that need some more work

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
# on some `od` implementations, extra spaces might be added, so `sed` them away
output_bvs="$(for bv in $bvs; do printf '%b' "\\0$bv" | ./rw; done |\
        od -to1 -An | tr -d '\n' | sed 's/  */ /g')"

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
get16c () { dd bs=1 count=16 2>/dev/null; }

total=$((total+1))
if [ "$(printf 0 | ./truthmachine)" = 0 ] && \
    [ "$(printf 1 | ./truthmachine | get16c)" = '1111111111111111' ]; then
        successes=$((successes+1))
        printf 'SUCCESS - truthmachine works for 0, and seems to work for 1. '
        printf '(Just looked at the first 16 bytes)\n'
else
    fails=$((fails+1))
    printf 'FAIL - truthmachine fails for at least one of its two inputs.\n'
fi

total=$((total+1))
if [ -n "$SKIP_DEAD_CODE" ]; then
    skipped=$((skipped+1))
    printf 'SKIPPED - did not compare dead_code.bf to null.bf as '
    printf 'SKIP_DEAD_CODE was set.\n'
elif diff null dead_code >/dev/null 2>&1; then
    successes=$((successes+1))
    printf 'SUCCESS - dead_code.bf optimized down to be identical to null.bf\n'
else
    fails=$((fails+1))
    printf 'FAIL - dead_code.bf not optimized down to be identical to null.bf\n'
fi

printf '########################\n'
printf 'SUCCESSES: %d\nFAILS: %d\nSKIPPED: %d\nTOTAL: %d\n' \
    "$successes" "$fails" "$skipped" "$total"

# as the last command, this sets the exit code for the script
[ "$fails" -eq 0 ]
