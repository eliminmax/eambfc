# SPDX-FileCopyrightText: 2023 - 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: GPL-3.0-only

# Generate the colortest_output.h file using a modified version of the AWK
# colortest implementation. Use "E" instead of "\033" and "N" instead of "\n",
# then replace them with "\\033" and "\\n" at the end, so that the right length
# can be calculated for the COLORTEST_OUTPUT_LEN macro, and the backslashes are
# left for C to handle

# parameters can't mirror variable names, and variables are all global
# workaround: ugly initialisms of function names as variable/parameter prefixes
function color_cell(ccn) { output = output "E[48;5;" ccn "m  " }
function cube_row_part(crpn) {
    for(crpi = crpn; crpi < (crpn + 6); crpi++) color_cell(crpi)
}
function cube_row(crn) {
    cube_row_part(crn)
    output = output "E[0m  "
    cube_row_part(crn + 36)
    output = output "E[0m  "
    cube_row_part(crn + 72)
    output = output "E[0mN"
}

BEGIN {
    # Handle the first 16 colors - these vary by terminal configuration
    output = "N"
    for (i = 0; i < 16; i++) color_cell(i)
    output = output "E[0mNN"

    # Handle the 6 sides of the color cube - these are more standardized
    # but the order is a bit odd, thus the need for the above trickery
    for (i = 16; i < 52; i += 6) cube_row(i)
    output = output "N"
    for (i = 124; i < 160; i += 6) cube_row(i)
    output = output "N"

    # Finally, the 24 grays
    for (i = 232; i < 256; i++) color_cell(i)
    output = output "E[0mNN"

    # now, use the output variable to generate the header

    outlen = length(output)
    print "/* header generated with colortest_output.awk */"
    print "/* clang-format off */"
    print "#define COLORTEST_OUTPUT_LEN " outlen
    printf  "#define COLORTEST_OUTPUT"
    # split, then replace the "E"s and "N"s, so that escapes are not split
    # across lines.
    # 54 chosen as the max width because it's the largest value that keeps
    # everything within 80 columns after "E"s and "N"s are replaced.
    for (i = 1; i <= outlen; i += 54) {
        snippet = substr(output, i, 54)
        gsub(/E/, "\\033", snippet)
        gsub(/N/, "\\n", snippet)
        printf " \\\n    \"%s\"", snippet
    }
    print
}
