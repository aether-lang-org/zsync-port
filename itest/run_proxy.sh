#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Colin Phipps <cph@moria.org.uk>
# SPDX-License-Identifier: Artistic-2.0
#
# Forward-proxy integration test: `--proxy URL` routes both the control
# fetch and every range fetch through an explicit HTTP proxy (Go's
# HTTP_PROXY behaviour; client.use_http_proxy, aether#1012). Proves the
# traffic actually traverses the proxy by inspecting its request log, and
# that the reconstruction is byte-identical.
#
# Replaces the original Go suite's tinyproxy scenario with a tiny inline
# Python forward proxy. Needs python3 + network; run sandbox disabled.

set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$HERE/build"
OPORT=${OPORT:-8095}
PPORT=${PPORT:-8096}
WORK="$(mktemp -d)"
trap 'kill -9 "$OSRV" "$PSRV" 2>/dev/null; rm -rf "$WORK"' EXIT
fail() { echo "FAIL: $1"; exit 1; }

python3 - "$WORK/data.bin" <<'PY'
import sys
s=7; out=bytearray()
for i in range(20000):
    s=(s*1103515245+12345)&0x7fffffff
    out.append((s>>16)&0xff)
open(sys.argv[1],'wb').write(bytes(out))
PY
mkdir -p "$WORK/srv"; cp "$WORK/data.bin" "$WORK/srv/data.bin"
"$BIN/zsyncmake" -b 1024 -o "$WORK/srv/data.bin.zsync" \
    -u "http://127.0.0.1:$OPORT/data.bin" -f data.bin "$WORK/data.bin" || fail zsyncmake

nohup "$BIN/fileserver" "$OPORT" "$WORK/srv" >"$WORK/o.log" 2>&1 & OSRV=$!

# minimal forward proxy: serves absolute-form GET, logging each request.
cat > "$WORK/proxy.py" <<PY
import http.server, urllib.request
class P(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        open("$WORK/hits.log","a").write(self.path+"\n")
        try:
            req=urllib.request.Request(self.path)
            for k,v in self.headers.items():
                if k.lower() not in ('proxy-connection','connection','host'):
                    req.add_header(k,v)
            r=urllib.request.urlopen(req); body=r.read()
            self.send_response(r.status)
            for k,v in r.headers.items():
                if k.lower() not in ('transfer-encoding','connection'):
                    self.send_header(k,v)
            self.end_headers(); self.wfile.write(body)
        except Exception as e:
            self.send_error(502, str(e))
    def log_message(self,*a): pass
http.server.HTTPServer(('127.0.0.1',$PPORT),P).serve_forever()
PY
nohup python3 "$WORK/proxy.py" >"$WORK/p.log" 2>&1 & PSRV=$!
sleep 1.5

python3 - "$WORK/data.bin" "$WORK/seed.bin" <<'PY'
import sys
d=bytearray(open(sys.argv[1],'rb').read())
for blk in range(0,20,4):
    o=blk*1024
    for i in range(o, min(o+1024,len(d))): d[i]^=0x5a
open(sys.argv[2],'wb').write(d)
PY

"$BIN/zsync" -q --proxy "http://127.0.0.1:$PPORT/" \
    -o "$WORK/out.bin" -i "$WORK/seed.bin" \
    "http://127.0.0.1:$OPORT/data.bin.zsync" || fail "client failed via --proxy"
cmp -s "$WORK/data.bin" "$WORK/out.bin" || fail "output differs from original"

# The proxy must have seen the traffic (control + at least one range).
hits=$(wc -l < "$WORK/hits.log" 2>/dev/null || echo 0)
[ "$hits" -ge 2 ] || fail "proxy saw only $hits requests — traffic did not traverse it"
echo "PASS: --proxy routed $hits requests through the proxy; reconstructed byte-identical"
