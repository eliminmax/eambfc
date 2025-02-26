#!/bin/sh
# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD
exec valgrind -q "$(dirname "$(dirname "$(realpath "$0")")")/eambfc" "$@"
