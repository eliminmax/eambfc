<!--
SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff

SPDX-License-Identifier: 0BSD
-->

# Code style

The following are the formatting I follow for source code:

* C: C89-style comments (i.e. `/* comment */`, not `// comment`)
* 80 character maximum per line, regardless of language.
* Indentation:
  * General: 4 spaces for indentation, except in the following cases:
    * Makefiles use 8-wide tab characters due to the constraints of the format
    * Markdown files use 2 spaces due to the constraints of the format
* Names:
  * constant macros, function-like macros, and enum variants are
    `SCREAMING_SNAKE_CASE`.
  * everything else is `snake_case` or `abrevnames`, depending on what fits.
    * this includes attribute macros
* C: `include`s are split into sections starting with one of the following:
  * `/* C99 */`: the header is defined by the C99 standard
    * either the C99 standard or the POSIX.1-2008 must require it to be present
    * POSIX.1-2008 extensions to the header's contents may be used.
  * `/* POSIX */`: the header is defined by and required by POSIX.1-2008
    * The C-Language Development extensions are assumed to be present.
  * `/* internal */`: the header is provided within the `eambfc` source tree.
  * Other libraries used in unit testing or non-portable optional features
    should use equivalent pre-include comments identifying their libraries.
* C: `clang-format` should be used to enforce a consistent style, and may only
  be disabled within generated headers.

The only exception is for brainfuck source code within the `test/` directory. It
has no formatting rules or style guides, but the code should include commentary
to explain what it's doing, how, and, if not originally written for this
project, where it came from.
