# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD

build:
	gcc -Wall -Werror serialize.c eam_compile.c main.c -o eambfc

debug_build:
	gcc -Wall -Werror -g3 -Og serialize.c eam_compile.c main.c -o eambfc

strict:
	gcc -D_POSIX_C_SOURCE=2 -static -pedantic -std=c99 -Wall -Werror serialize.c eam_compile.c main.c -o eambfc
	clang-16 -D_POSIX_C_SOURCE=2 -static -pedantic -std=c99 -Wall -Werror serialize.c eam_compile.c main.c -o eambfc
	s390x-linux-gnu-gcc -D_POSIX_C_SOURCE=2 -pedantic -std=c99 -Wall -Werror serialize.c eam_compile.c main.c -o eambfc-s390x

ubdetect:
	gcc -D_POSIX_C_SOURCE=2 -pedantic -std=c99 -Wall -Werror -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all -fsanitize=float-divide-by-zero -fsanitize=float-cast-overflow -fno-sanitize=null -fno-sanitize=alignment serialize.c eam_compile.c main.c -o eambfc

clean:
	rm -f eambfc eambfc-s390x
