#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Colin Phipps <cph@moria.org.uk>
# SPDX-License-Identifier: Artistic-2.0
#
# Conditional-GET (`-k` resume) integration test. The fileserver now emits
# Last-Modified and honours If-Modified-Since -> 304, so the client's -k
# path is exercised end-to-end:
#   1. server GET emits Last-Modified; a matching IMS gets 304, an older
#      IMS gets 200 (raw curl checks).
#   2. `zsync -k keep.zsync` run twice: run 1 downloads and stamps keep's
#      mtime from Last-Modified; run 2 sends that mtime as IMS and the
#      server replies 304, so the client reuses the local copy.
#
# Needs curl + network; run with the sandbox disabled.

set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$HERE/build"
PORT=${PORT:-8097}
WORK="$(mktemp -d)"
trap 'kill -9 "$SRV" 2>/dev/null; rm -rf "$WORK"' EXIT
fail() { echo "FAIL: $1"; exit 1; }

python3 - "$WORK/data.bin" <<'PY'
import sys
s=3; out=bytearray()
for i in range(20000):
    s=(s*1103515245+12345)&0x7fffffff
    out.append((s>>16)&0xff)
open(sys.argv[1],'wb').write(bytes(out))
PY
mkdir -p "$WORK/srv"; cp "$WORK/data.bin" "$WORK/srv/data.bin"
"$BIN/zsyncmake" -b 1024 -o "$WORK/srv/data.bin.zsync" \
    -u "http://127.0.0.1:$PORT/data.bin" -f data.bin "$WORK/data.bin" || fail zsyncmake
# fixed, in-the-past mtime so the conditional logic is deterministic
touch -d '2026-01-01T00:00:00Z' "$WORK/srv/data.bin.zsync"

nohup "$BIN/fileserver" "$PORT" "$WORK/srv" >"$WORK/srv.log" 2>&1 & SRV=$!
sleep 1

# 1. raw conditional-GET behaviour.
lm=$(curl -s -D - -o /dev/null "http://127.0.0.1:$PORT/data.bin.zsync" | grep -i '^last-modified:' | sed 's/^[Ll]ast-[Mm]odified: //' | tr -d '\r')
[ -n "$lm" ] || fail "server did not emit Last-Modified on GET"
code=$(curl -s -o /dev/null -w '%{http_code}' -H "If-Modified-Since: $lm" "http://127.0.0.1:$PORT/data.bin.zsync")
[ "$code" = "304" ] || fail "matching If-Modified-Since got $code, want 304"
code=$(curl -s -o /dev/null -w '%{http_code}' -H "If-Modified-Since: Mon, 01 Jan 2024 00:00:00 GMT" "http://127.0.0.1:$PORT/data.bin.zsync")
[ "$code" = "200" ] || fail "older If-Modified-Since got $code, want 200"
echo "PASS: server emits Last-Modified and honours If-Modified-Since (304/200)"

# 2. client -k round trip: run 1 caches, run 2 gets 304.
"$BIN/zsync" -q -k "$WORK/keep.zsync" -o "$WORK/o1.bin" -i "$WORK/data.bin" \
    "http://127.0.0.1:$PORT/data.bin.zsync" || fail "run 1 client failed"
[ -f "$WORK/keep.zsync" ] || fail "run 1 did not save the -k copy"

out=$("$BIN/zsync" -k "$WORK/keep.zsync" -o "$WORK/o2.bin" -i "$WORK/data.bin" \
        "http://127.0.0.1:$PORT/data.bin.zsync" 2>&1) || fail "run 2 client failed"
echo "$out" | grep -qi 'not modified' || fail "run 2 did not hit the 304 path: $out"
cmp -s "$WORK/data.bin" "$WORK/o2.bin" || fail "run 2 output differs from original"
echo "PASS: -k reuses the local control file via a 304 on the second run"
