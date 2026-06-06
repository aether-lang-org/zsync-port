# SPDX-FileCopyrightText: 2026 Colin Phipps <cph@moria.org.uk>
# SPDX-License-Identifier: Artistic-2.0
#
# Build + test driver for the pure-Aether zsync port. Uses the freshly
# built aether toolchain (which has MD4, issue #637) rather than the
# possibly-stale installed `ae`. No aeb dependency — plain make + ae.

AE      ?= /home/paul/scm/aether/build/ae
AETHERC ?= /home/paul/scm/aether/build/aetherc
CFLAGS_AE := $(shell $(AE) cflags)
CC      ?= cc

# Sidecar C objects (positional I/O shim).
C_OBJS := rcksum/fileio.o

%.o: %.c
	$(CC) -c $< -o $@

# Run a single .ae test through `ae run`, linking sidecar C via the
# build pipeline. For tests that need the C shim we go through aetherc
# + cc; pure-Aether tests can use `ae run` directly.

.PHONY: test test-pure test-fileio clean

# Pure-Aether leaf tests (no sidecar C needed).
test-pure:
	$(AE) run rcksum/ranges_test.ae
	$(AE) run rcksum/checksums_test.ae

# Tests that link the C shim: compile .ae -> .c, then cc with the shim.
# $(AETHERC) emits a .c next to the source; we compile+link it with the
# sidecar object and the aether runtime.
test-fileio: $(C_OBJS)
	@mkdir -p build
	$(AETHERC) rcksum/fileio_test.ae build/fileio_test.c
	$(CC) build/fileio_test.c $(C_OBJS) $(CFLAGS_AE) -o build/fileio_test
	./build/fileio_test

test: test-pure

clean:
	rm -f rcksum/*.o build/*
