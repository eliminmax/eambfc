# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD

build:
	gcc -Wall -Werror eam_compile.c main.c -o eambfc

debug_build:
	gcc -Wall -Werror -g3 -Og eam_compile.c main.c -o eambfc

clean:
	rm -f eambfc
