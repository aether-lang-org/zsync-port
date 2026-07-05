#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Colin Phipps <cph@moria.org.uk>
# SPDX-License-Identifier: Artistic-2.0
#
# TLS integration test: --no-check-certificate against a self-signed HTTPS
# server. Proves two things end-to-end:
#   1. WITHOUT the flag, the client REJECTS the self-signed cert (TLS verify
#      is on by default — a security property).
#   2. WITH the flag, the download succeeds over self-signed HTTPS and
#      reconstructs byte-identical (client.set_insecure, aether#1012, wired
#      through both the control fetch and the parallel range fetches).
#
# Replaces the original Go suite's Apache-with-self-signed-cert scenario.
# Needs openssl + python3 + network; run with the sandbox disabled.

set -u
AE=${AE:-/home/paul/scm/aether/build/ae}
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$HERE/build"
PORT=${PORT:-8443}
WORK="$(mktemp -d)"
trap 'kill -9 "$SRV" 2>/dev/null; rm -rf "$WORK"' EXIT

fail() { echo "FAIL: $1"; exit 1; }

# self-signed cert for localhost
openssl req -x509 -newkey rsa:2048 -keyout "$WORK/key.pem" -out "$WORK/cert.pem" \
    -days 1 -nodes -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" 2>/dev/null \
    || fail "openssl cert generation"

# deterministic, non-degenerate data
python3 - "$WORK/data.bin" <<'PY'
import sys
s=99; out=bytearray()
for i in range(40000):
    s=(s*1103515245+12345)&0x7fffffff
    out.append((s>>16)&0xff)
open(sys.argv[1],'wb').write(bytes(out))
PY
mkdir -p "$WORK/srv"; cp "$WORK/data.bin" "$WORK/srv/data.bin"

"$BIN/zsyncmake" -b 1024 -o "$WORK/srv/data.bin.zsync" \
    -u "https://localhost:$PORT/data.bin" -f data.bin "$WORK/data.bin" \
    || fail "zsyncmake"

# minimal Range-capable HTTPS server over the self-signed cert
cat > "$WORK/server.py" <<PY
import http.server, ssl, os
os.chdir("$WORK/srv")
class H(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        rng = self.headers.get('Range')
        path = self.translate_path(self.path)
        if not os.path.exists(path): self.send_error(404); return
        data = open(path,'rb').read()
        if rng and rng.startswith('bytes='):
            a,b = rng[6:].split('-'); a=int(a); b=int(b) if b else len(data)-1
            chunk=data[a:b+1]
            self.send_response(206)
            self.send_header('Content-Range', f'bytes {a}-{b}/{len(data)}')
            self.send_header('Content-Length', str(len(chunk)))
            self.end_headers(); self.wfile.write(chunk)
        else:
            self.send_response(200)
            self.send_header('Content-Length', str(len(data)))
            self.end_headers(); self.wfile.write(data)
    def log_message(self,*a): pass
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain("$WORK/cert.pem","$WORK/key.pem")
httpd=http.server.HTTPServer(('127.0.0.1',$PORT),H)
httpd.socket=ctx.wrap_socket(httpd.socket,server_side=True)
httpd.serve_forever()
PY
nohup python3 "$WORK/server.py" >"$WORK/srv.log" 2>&1 &
SRV=$!
sleep 1.5

# partial seed so a real Range download happens
python3 - "$WORK/data.bin" "$WORK/seed.bin" <<'PY'
import sys
d=bytearray(open(sys.argv[1],'rb').read())
for blk in range(0,40,3):
    o=blk*1024
    for i in range(o, min(o+1024,len(d))): d[i]^=0x5a
open(sys.argv[2],'wb').write(d)
PY

# 1. WITHOUT the flag: must fail to verify the self-signed cert.
if "$BIN/zsync" -q -o "$WORK/out1.bin" -i "$WORK/seed.bin" \
       "https://localhost:$PORT/data.bin.zsync" 2>/dev/null; then
    fail "self-signed cert was accepted WITHOUT --no-check-certificate"
fi
[ -f "$WORK/out1.bin" ] && fail "output written despite TLS verify failure"
echo "PASS: self-signed HTTPS rejected without --no-check-certificate"

# 2. WITH the flag: must succeed and reconstruct byte-identical.
"$BIN/zsync" -q --no-check-certificate -o "$WORK/out2.bin" -i "$WORK/seed.bin" \
    "https://localhost:$PORT/data.bin.zsync" || fail "client failed with --no-check-certificate"
cmp -s "$WORK/data.bin" "$WORK/out2.bin" || fail "output differs from original"
echo "PASS: --no-check-certificate downloads + reconstructs over self-signed HTTPS"
