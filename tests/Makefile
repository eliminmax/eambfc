# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD
# vi: noet sw=4 ts=4 sts=4 cc=81

.POSIX:
.SUFFIXES: .bf
.bf:
	../eambfc -k $*.bf 2>.$*.build_err && rm .$*.build_err

# build test assets
build-all: hello loop null rw wrap wrap2 colortest truthmachine \
	too-many-nested-loops

hello: hello.bf
loop: loop.bf
null: null.bf
wrap: wrap.bf
wrap2: wrap2.bf
colortest: colortest.bf
truthmachine: truthmachine.bf
too-many-nested-loops: too-many-nested-loops.bf

# expect to encounter a compiler error for these, don't treat as a failure
.IGNORE: too-many-nested-loops

clean:
	rm -f .*.build_err \
		hello rw loop null wrap wrap2 colortest truthmachine \
		too-many-nested-loops
