# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD

build:
	gcc -Wall -Werror eam_compile.c main.c -o eambfc

debug_build:
	gcc -Wall -Werror -g3 -Og eam_compile.c main.c -o eambfc

strict:
	gcc -D_POSIX_C_SOURCE=2 -pedantic -std=c99 -Wall -Werror eam_compile.c main.c -o eambfc -fsso-struct='little-endian'

clean:
	rm -f eambfc
