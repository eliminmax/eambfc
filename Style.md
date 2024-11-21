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
    * Makefiles use 8-wide tab characters due to the constraints of the format
    * Markdown files use 2 spaces due to the constraints of the format
* C: Open braces are on the same line as the function signature/conditional/etc.
* C: Multi-line comments have an asterisk at the start of each line.
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

There's a .clang-format file for clang-format-16, which is the latest verison
available in Debian Bookworm. If installed, a pre-push hook can be created (by
default at `"$GIT_DIR/hooks/pre-push"`) with the following contents to ensure
that all C source and header files except `compat/elf.h` are formatted according
to the rules in `.clang-format` before pushing. I have pre-push and pre-commit
hooks set up.

```sh
#!/bin/sh
cd "$(git rev-parse --show-toplevel)" || exit 1

clang-format-16 -Werror -n *.[ch] compat/*.h
```

There are two cases where those formatting rules don't apply.

The first is in `compat/elf.h` - it's adapted from glibc's `elf.h`, and its
style is a more arbitrary hybrid of the original and my own style, and should be
hand-adjusted to ensure internal consistency within the header.

Brainfuck source code in the `test/` directory is the other exception - it has
no formatting rules or style guides, but the code should include commentary to
explain what it's doing, how, and, if not written for this project, where it
came from.

