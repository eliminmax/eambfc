# SPDX-FileCopyrightText: 2025 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD

runmatch: runmatch.c
	$(CC) -D _POSIX_C_SOURCE=200908L $(CFLAGS) $(LDFLAGS) \
		runmatch.c -o $@ $(LDLIBS)

clean:
	rm -f runmatch
