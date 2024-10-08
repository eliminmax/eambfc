<!--
SPDX-FileCopyrightText: 2024 Eli Array Minkoff

SPDX-License-Identifier: 0BSD
-->

# Code style

The following are the formatting I follow for source code:

* C: C89-style comments (i.e. `/* comment */`, not `// comment`)
* 80 character maximum per line, regardless of language.
* Indentation:
  * General: 4 spaces for indentation, except in the following cases:
    * C: `case`s within `switch` statements are half indented
    * Makefiles use 8-wide tab characters due to the constraints of the format
    * Markdown files use 2 spaces due to the constraints of the format
* C: Open braces are on the same line as the function signature/conditional/etc.
* C: Multi-line comments have an asterisk at the start of each line.
  * Exception: license heading in `compat/elf.h` is left as-is.
* Names:
  * macros, enum variants, and const struct members are `SCREAMING_SNAKE_CASE`.
  * everything else is `snake_case` or `abrevnames`, depending on what fits.
* C: `include`s are split into sections starting with one of the following:
  * `/* C99 */`: the header is defined by the C99 standard
    * either the C99 standard or the POSIX.1-2008 must require it to be present
    * POSIX.1-2008 extensions to the header's contents may be used.
  * `/* POSIX */`: the header is defined by and required by POSIX.1-2008
    * The C-Language Development extensions are assumed to be present.
  * `/* internal */`: the header is provided within the `eambfc` source tree.
  * Each `#include` are accompanied by a comment explaining why it's there.

Most of the code in this repository was written specifically for this project,
and follows the formatting and style rules. Code originally from other projects
may or may not be adapted to fit some or all of the formatting and style rules.

Brainfuck source code in the `test/` directory is the exception - it has no
formatting rules or style guides, but the code should include commentary to
explain what it's doing, how, and, if not written for this project, where it
came from.
