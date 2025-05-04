<!--
SPDX-FileCopyrightText: 2025 Eli Array Minkoff

SPDX-License-Identifier: GPL-3.0-only
-->

# Project Structure

## File Hierarchy

The organization of the project is driven primarily by the limitations of POSIX
`make` - because POSIX `make` does not support targets that depend on files in
other directories, and doesn't make recursive `make` easy to use, the C source
code for the `eambfc` binary needs to be in a single, flat directory, and early
on, that directory was the project root, and that has not changed.

The directory structure is as follows:
* `.` - project root
  * `docs` - documentation about the project
  * `include` - project-wide include files
  * `LICENSES` - license files, stored according to the REUSE standard
  * `tests` - test brainfuck programs, as well as the code for `test_driver`
  * `tools` - miscellaneous tools and resources used
    * `execfmt_support` - tiny binaries used to check if some tests can be run

## Code structure

### General

There are 2 conceptual parts to the execution of `eambfc` - the setup and the
compilation process, and the `main` function (in `main.c`) is used to launch
them and bridge the gap between them.

The setup phase processes command-line arguments, configures the error reporting
mode, and handles alternate execution paths, such as displaying help for the
`-h` flag. Assuming that command-line flags are valid and coherent, and that no
alternate paths were taken, it then returns the runtime configuration to the
`main` function, including the backend to use and flags to tweak error-handling
behavior, and a flag to enable or disable optimization.

The `main` function then uses the `bf_compile` function to compile the output
binaries with the provided backend.

The struct used to store runtime configuration info is called `run_cfg`, and is
defined in `setup.h`.

### Backends

Backends are implemented primarily with the `ArchInter` struct defined in
`arch_inter.h`, which provides the architecture-specific information and codegen
functions that each architecture needs, and `include/backends.h`, which (ab)uses
the C preprocessor to implement the interface to the backends. The majority of
the places that need to do something for each backend in use
`include/backends.h`, and those that don't are marked with a comment containing
the text "`__​BACKENDS​__`" alongside a brief explanation of what
needs to be added.

*(The above instance of the string has zero-width spaces embedded within it, to
ensure that it doesn't show up in a `git grep` for the string)*
