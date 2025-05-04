<!--
SPDX-FileCopyrightText: 2024 - 2025 Eli Array Minkoff

SPDX-License-Identifier: 0BSD
-->

# Code style

# General

Other than brainfuck source code in the `test/` directory and license files in
the LICENSES directory, all files should fit within 80 characters. (Tabs are
considered to be 8 wide for that purpose). Justfiles and Markdown use 2 space
indents. Makefiles use single tab indents. All other files use 4-space indents.

## C

`clang-format` should be used to enforce a consistent style, and may only be
disabled within generated headers.

### Comments

Comments should be `/* C89-style */`, rather than `// C99 style`.

### Naming Things

Names are either `SCREAMING_SNAKE_CASE`, `snake_case`, or `PascalCase`,
depending on what is being named. The default choice is `snake_case`.

The three exceptions are custom types, function-like macros, and constants.

A `typedef` of a `struct`, `union`, `enum`, or function pointer should be
`PascalCase`.

Constants (both `const` variable declarations and macros) should be
`SCREAMING_SNAKE_CASE`.

Function-like macros should be `SCREAMING_SNAKE_CASE`

If an `enum`, `struct` or `union` would never be referred to by name *(i.e. it's
defined inline within a `typedef`, variable declaration or larger `struct` or
`union`)*, then its name should be omitted entirely, as it would be redundant.

### Headers

* C: `include`s are split into sections starting with one of the following:
* `/* C99 */`: the header is defined by the C99 standard
  * either the C99 standard or the POSIX.1-2008 must require it to be present
  * POSIX.1-2008 extensions to the header's contents may be used.
* `/* POSIX */`: the header is defined by and required by POSIX.1-2008
  * The C-Language Development extensions are assumed to be present.
* `/* internal */`: the header is provided within the `eambfc` source tree.
* Other libraries used in unit testing or non-portable optional features should
  use equivalent pre-include comments identifying their libraries.
