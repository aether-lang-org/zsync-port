#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Colin Phipps <cph@moria.org.uk>
# SPDX-License-Identifier: Artistic-2.0
#
# Regenerate the committed unit-test fixtures from the Go oracle.
#
# These fixtures used to live in /tmp and were hand-made during the original
# port session; a wiped /tmp left the parser/download/zsyncmake tests reading
# missing files and segfaulting. They are now committed (this directory) and
# reproducible, so `make test` is green on a clean checkout. Run this only if
# you need to regenerate them (e.g. the fixture format changes).
#
# Requires: `go` on PATH, and the `legacy_golang` branch (the parity oracle).
#
# Determinism:
#   - ctltest.dat is 5000 bytes from the SAME LCG the itest uses
#     (seed 12345, s=(s*1103515245+12345)&0x7fffffff, byte (s>>16)&0xff).
#     Its SHA-1 is f5d0ac38bfdafcbc2bfe68aad7af017c6b8ad946 — the value
#     hardcoded in zsync/control_test.ae and download_test.ae.
#   - The input mtime is pinned to 2026-01-01T00:00:00Z so the MTime header
#     (and thus every generated .zsync byte) is stable.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# 1. The canonical data file (deterministic, non-degenerate LCG).
python3 - "$WORK/ctltest.dat" <<'PY'
import sys
s=12345; out=bytearray()
for i in range(5000):
    s=(s*1103515245+12345)&0x7fffffff
    out.append((s>>16)&0xff)
open(sys.argv[1],'wb').write(bytes(out))
PY
touch -d '2026-01-01T00:00:00Z' "$WORK/ctltest.dat"

# 2. Build the Go oracle's zsyncmake from the pristine legacy branch.
GO_SRC="$WORK/go"; mkdir -p "$GO_SRC"
git -C "$REPO" archive legacy_golang | tar -x -C "$GO_SRC"
( cd "$GO_SRC" && go build -buildvcs=false -o "$WORK/go_zsyncmake" ./cmd/zsyncmake )

# 3. ctltest.dat.zsync — relative-URL form, used by control_test + download_test.
( cd "$WORK" && "$WORK/go_zsyncmake" -b 1024 "$WORK/ctltest.dat" >/dev/null 2>&1 )

# 4. go_ref.zsync — the byte-parity reference for zsyncmake_test (explicit -u/-f).
"$WORK/go_zsyncmake" -b 1024 -o "$WORK/go_ref.zsync" \
    -u http://example/ctltest.dat -f ctltest.dat "$WORK/ctltest.dat"

# 5. go_mtime.txt — the exact MTime header string, fed back to mklib in the test.
python3 - "$WORK/go_ref.zsync" "$WORK/go_mtime.txt" <<'PY'
import sys
mt=""
for line in open(sys.argv[1],'rb').read().split(b'\n'):
    if line.startswith(b'MTime: '):
        mt=line[len(b'MTime: '):].decode(); break
open(sys.argv[2],'w').write(mt+'\n')
PY

cp "$WORK/ctltest.dat" "$WORK/ctltest.dat.zsync" "$WORK/go_ref.zsync" "$WORK/go_mtime.txt" "$HERE/"
echo "Regenerated fixtures in $HERE:"
( cd "$HERE" && sha1sum ctltest.dat ctltest.dat.zsync go_ref.zsync go_mtime.txt )
