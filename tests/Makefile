# SPDX-FileCopyrightText: 2024 Eli Array Minkoff
#
# SPDX-License-Identifier: 0BSD
# vi: noet sw=4 ts=4 sts=4 cc=81

EAMBFC ?= ../eambfc
.POSIX:
.SUFFIXES: .bf
.bf:
	$(EAMBFC) -kj $*.bf >.$*.build_err && rm .$*.build_err

# build test assets
build-all: hello loop null rw wrap wrap2 colortest truthmachine \
	too-many-nested-loops unmatched-close unmatched-open \
	unseekable alternative-extension

test: clean build-all
	./test.sh $(EAMBFC)

hello: hello.bf
loop: loop.bf
null: null.bf
wrap: wrap.bf
wrap2: wrap2.bf
colortest: colortest.bf
truthmachine: truthmachine.bf
too-many-nested-loops: too-many-nested-loops.bf
unmatched-close: unmatched-close.bf
unmatched-open: unmatched-open.bf
# test error from unseekable outputs
unseekable:
	mkfifo unseekable
	($(EAMBFC) -kj unseekable.bf & tail unseekable >/dev/null; wait) \
		>.unseekable.build_err
# test support for alternative extensions
alternative-extension:
	$(EAMBFC) -kj -e .brnfck alternative-extension.brnfck


# expect to encounter a compiler error for these, don't treat as a failure
.IGNORE: too-many-nested-loops unmatched-close unmatched-open

clean:
	rm -f .*.build_err \
		hello rw loop null wrap wrap2 colortest truthmachine \
		too-many-nested-loops unmatched-close unmatched-open \
		unseekable alternative-extension
