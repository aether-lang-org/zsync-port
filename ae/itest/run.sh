#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Colin Phipps <cph@moria.org.uk>
# SPDX-License-Identifier: Artistic-2.0
#
# End-to-end integration test for the pure-Aether zsync, replacing the
# Go suite's Apache dependency with the native Aether file server.
#
# Flow: zsyncmake generates a .zsync for a data file; the native
# fileserver serves both over HTTP with Range/206 support; the zsync
# client fetches the .zsync over HTTP, seeds from a deliberately-altered
# partial copy, fetches the missing byte ranges over HTTP, and the
# reconstructed output must byte-match the original.
#
# Usage: ae/itest/run.sh   (run from the repo's ae/ dir after `make bins`)

set -u
AE=${AE:-/home/paul/scm/aether/build/ae}
HERE="$(cd "$(dirname "$0")/.." && pwd)"     # the ae/ dir
BIN="$HERE/build"
PORT=${PORT:-8079}
WORK="$(mktemp -d)"
trap 'kill -9 "$SRV" 2>/dev/null; rm -rf "$WORK"' EXIT

fail() { echo "FAIL: $1"; exit 1; }

# 1. deterministic, non-degenerate data file (LCG so blocks differ).
python3 - "$WORK/data.bin" <<'PY'
import sys
s=12345; out=bytearray()
for i in range(5000):
    s=(s*1103515245+12345)&0x7fffffff
    out.append((s>>16)&0xff)
open(sys.argv[1],'wb').write(bytes(out))
PY

mkdir -p "$WORK/srv"
cp "$WORK/data.bin" "$WORK/srv/data.bin"

# 2. generate the .zsync (native tool), URL pointing at the served file.
"$BIN/zsyncmake" -b 1024 -o "$WORK/srv/data.bin.zsync" \
    -u "http://127.0.0.1:$PORT/data.bin" -f data.bin "$WORK/data.bin" \
    || fail "zsyncmake"

# 3. partial seed: original with the first 2048 bytes perturbed.
python3 - "$WORK/data.bin" "$WORK/seed.bin" <<'PY'
import sys
d=bytearray(open(sys.argv[1],'rb').read())
for i in range(2048): d[i]=(d[i]+1)&0xff
open(sys.argv[2],'wb').write(d)
PY

# 4. serve.
nohup "$BIN/fileserver" "$PORT" "$WORK/srv" >"$WORK/srv.log" 2>&1 &
SRV=$!
sleep 2

# 5. download via the client (fetches .zsync over HTTP, ranges over HTTP).
"$BIN/zsync" -o "$WORK/out.bin" -i "$WORK/seed.bin" \
    "http://127.0.0.1:$PORT/data.bin.zsync" || fail "client exited non-zero"

# 6. verify byte-identical reconstruction.
cmp -s "$WORK/data.bin" "$WORK/out.bin" || fail "output differs from original"

echo "PASS: pure-Aether zsync downloaded + reconstructed via HTTP Range (5000 bytes, SHA-1 verified)"
