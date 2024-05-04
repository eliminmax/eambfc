<!--
SPDX-FileCopyrightText: 2024 Eli Array Minkoff

SPDX-License-Identifier: 0BSD
-->

# Eli Array Minkoff's BFC

A non-optimizing compiler for brainfuck, written in C.

Outputs an x86_64 ELF executable that uses Linux system calls.

I am not an experienced C programmer, and this is an attempt to gain practice by writing something somewhat simple yet not trivial.

At time of writing, it can compile the `+`, `-`, `>`, `<`, `,`, and `.` instructions, but errors out if it encounters `[` or `]`. This is by design, as I have not started working on them yet.
