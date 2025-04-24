#!/bin/sh

# SPDX-FileCopyrightText: 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD

# use [_] so that the pattern doesn't match itself
cd "$(git rev-parse --show-toplevel)" && exec git grep -A3 '[_]_BACKENDS_[_]'
