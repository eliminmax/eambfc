<!--
SPDX-FileCopyrightText: 2025 Eli Array Minkoff

SPDX-License-Identifier: 0BSD
-->

# About these binaries

These binaries each simply call the exit system call with exit code 0, to check
whether or not the system this is running on can test binaries for the relevant
architectures.

They were all made by hand in a hex editor.

With the exception of the `i386` binary, which is custom-build, they are adapted
from my [tiny-clear-elf](https://github.com/eliminmax/tiny-clear-elf) project.

If anyone is concerned by their inclusion, I understand, and I wrote the
`check_bins.py` script in python to help anyone who wants to audit them. While
the binaries themselves were created by hand using a minimal hex editor, the
`check_bins.py` script can be used to check that they are what I claim them to
be, using only Python on a Linux system, and its docstring lists places to
double-check the constants and registers used within everything.

<!-- __BACKENDS__ add a new backend binary for the architecture -->
