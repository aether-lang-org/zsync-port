# Notes to self (LLM on the Aether zsync port)

Not a CLAUDE.md. Skinny, opinionated, for a sibling Claude picking this up
mid-task. Re-read at session start.

## What this is

Pure-Aether port of zsync (rsync-over-HTTP: download a file fetching only
the changed blocks). ~3.4k lines Aether + one small C shim. Ported by a
sibling Claude in ~4h, parity-gated against the original Go. Artistic-2.0 (NOT
MIT). Keep zsync's import graph free of aeocha/aeb *modules* — but that's an
import-graph rule, NOT an artifact-license one: a build TOOL (aeb, like make or
gcc) doesn't relicense the output, so zsync could be built BY aeb and stay
Artistic. What's forbidden is `import`ing MIT aeb/aeocha modules into Artistic
zsync source — and aeb vendoring zsync *source* the other way. Linking is fine
both ways (Artistic-2.0 §7/§8): aeb may link a zsync `.so`.

- `main` = the Aether port (this). `legacy_golang` = pristine original Go,
  untouched, your parity oracle.
- Origin: `git@github.com:aether-lang-org/zsync-port.git`. `main` is default.

## Build / test — Makefile (ae) OR aeb

**Makefile path:** `/home/paul/scm/aether/build/ae`, the dev tree. (Historically
"NOT /usr/local/bin/ae — lacks MD4". That note is now STALE: installed Aether
≥0.256 HAS md4_hex (#637 landed ≥0.218). Either works for the Makefile; the
Makefile pins `AE` at build/ae for stability.)

**aeb path (2026-06):** zsync now also builds with aeb — `aeb cmd/zsync/.build.ae`
(+ zsyncmake/fileserver). aeb compiles each module `aetherc --emit=lib` (stricter
than the Makefile's whole-program compile). Build against the INSTALLED Aether
(`/usr/local/bin/ae`), which has the nested include layout aeb wants — building
against the dev TREE currently hits an aeb include-threading gap (aether_panic.h
not found; filed as aeb asks/aether-program-dev-tree-include-threading.md). aeb
is a build TOOL here — it does NOT relicense the Artistic-2.0 output (see top).
server_dsl_example is pre-broken (DSL closure form) in `make` too — not aeb's fault.

- `make test` → 7 test files, ~94 asserts, all parity-checked vs Go. Green = good.
- `make bins` → build/{zsync,zsyncmake,fileserver,server_dsl_example}.
- `make itest` → full client+server HTTP round-trip (needs network; run with
  sandbox disabled). Prints PASS.
- Pure-Aether leaf tests run via `ae run`; anything touching the C shim goes
  aetherc→cc→link (the Makefile's `build/%` rules do this).
- **Test fixtures are COMMITTED in `test/fixtures/`** (`ctltest.dat` +
  `.zsync`, `go_ref.zsync`, `go_mtime.txt`). control/download/zsyncmake tests
  read them by RELATIVE path (`test/fixtures/…`), so run from the repo root
  (make does). They used to be hand-made in `/tmp` — a wiped `/tmp` made those
  three tests read null buffers and **segfault before any assert printed**
  (looked like a protocol regression; wasn't). If they ever go missing again a
  test now fails readably (`assert.fatal`) instead of crashing. `ctltest.dat`
  is the same LCG the itest uses (seed 12345) → SHA-1 f5d0ac38…, the value
  hardcoded in control/download tests. Regenerate: `test/fixtures/regen.sh`
  (needs `go` + the `legacy_golang` branch).
- Regenerate a Go oracle: `git archive legacy_golang | tar -x -C /tmp/go &&
  cd /tmp/go && go build -buildvcs=false -o /tmp/go_zsync ./cmd/zsync` (etc).

## Layout

- `rcksum/` — the engine. `ranges` (block-range set), `checksums` (Adler
  rsum + MD4 + rhash + log2), `rcksum` (hash tables + bithash + rolling
  matcher + scatter-write), `fileio.{ae,c}` (the C shim: positional I/O,
  mutable byte buffers, dup16, slice/pad, RFC1123Z).
- `zsync/` — `control` (.zsync parser), `download` (high-level State; named
  `download` because `state` is reserved).
- `cmd/` — `zsync` (client) + `clientlib`; `zsyncmake` + `mklib` (generator);
  `fileserver` (native Range server); `serverdsl` + `server_dsl_example`
  (the config-IS-code closure-DSL surface).
- `test/assert.ae` — tiny in-tree harness (`new/ok/eq_int/eq_str/report`,
  exits 1 on fail). `itest/run.sh` — e2e.

## Aether gotchas that WILL bite (all learned the hard way here)

- **Call forms.** Raw externs: glob-import + unqualified — `import std.list (*)`
  then `list_new()`. Tuple-returning wrappers: import plain + qualified —
  `import std.cryptography` then `cryptography.md4_bytes(...)`. Getting it
  backwards gives "Undefined function 'list.list_new'" or a broken tuple
  destructure. Often you want BOTH imports of one module in a file.
- **Modules ref'd by LAST path component:** `import rcksum.ranges` → call
  `ranges.count()`, NOT `rcksum.ranges.count()` (→ "Undefined function '?'").
- **`as` only targets `*Struct` / `fn(...)` / `T[]`.** No `as ptr`, `as int`,
  `as string`. Scalars: rely on implicit int↔long widen, or a passthrough fn
  `f2i(x: float) -> int { return x }` for float→int. ptr↔string: just return
  it (both char* in C). Member access on a parenthesized cast can mis-parse —
  bind to a temp first (`s = z as *Foo; return s.x`).
- **Reserved words bite module FILENAMES too:** `state`, `match`, `message`,
  `after`. `import pkg.state` fails at the import line even if state.ae is
  trivial. (That's why it's `download.ae`.)
- **Top-level fn names must be globally unique across ALL transitively
  imported modules** — even called qualified. `state.new` vs `rcksum.new`
  collided → misleading "Expected actor, struct, function, or main" at the
  import line. Name port fns distinctly. Also watch collisions with stdlib
  glob symbols (`to_int` clashed → renamed `parse_num`).
- **`string_substring` / `string_concat` are strlen-based — TRUNCATE at the
  first NUL.** Useless on binary (blocks, digests). Use `fileio.slice` /
  length-explicit ops everywhere binary flows. `string_char_at_n(s, len, i)`
  reads binary fine (explicit length).
- **Heap-string tracker recycles AetherStrings stored in hand-malloc'd
  struct fields** — even after `string_retain`. Caused a deterministic +2
  shift in stored block MD4s (int fields fine, only the string field). Fix:
  for long-lived bytes in untracked structs, return a raw malloc'd char*
  typed as string from C (NOT via string_new_with_length) — no magic header,
  tracker leaves it alone. See `zsync_dup16`/`fileio.dup16`.
- **Don't `extern pwrite` (etc.) directly** — Aether emits a clashing
  prototype vs glibc. Sidecar C with uniquely-named wrappers (`zsync_io_*`),
  extern those. `_GNU_SOURCE` for strptime/timegm.
- **`@extern("c_name") fn(...)` — no `extern` keyword after the annotation.**
  And `@extern`-defined fns aren't reachable cross-module as `mod.fn`; wrap
  in a normal fn if another module needs them.
- **Parity tests need non-degenerate data.** First fixture `(i*37+11)%256`
  made every 1024-block byte-identical (period 256) and masked a real bug.
  Use an LCG/PRNG.

## std.http facts relevant here

- Server route dispatch: registration order, first match wins.
  `http_route_matches` = exact strcmp, then `*` wildcard. Register specific
  routes before a `/*` catch-all.
- `serve_static(req,res,base)` honours Range → 206 (#641) AND has a `..`
  traversal guard, but maps the FULL request path under base (no prefix
  strip). `serve_file(res,path)` takes no request → **ignores Range** (always
  200). So under a route prefix you must either mirror dirs or implement
  Range yourself — `serverdsl.serve_one` does the latter (parse Range, slice
  via fileio, 206 + Content-Range, own `..` guard).
- Client v2 (`std.http.client`): `request(m,url)` → `set_header` →
  `send_request` → `response_status`/`response_body_length`/`response_body`.
  Range = `set_header(req,"Range","bytes=a-b")`, expect 206. No TLS-verify-
  skip toggle yet (so `--no-check-certificate` is a no-op).

## Upstream stdlib gaps this port filed (all landed ≥0.218)

#637 MD4 + binary digests · #640 std.fs positional I/O
(pwrite/pread/ftruncate/fsync) · #641 serve_static Range. If you hit a new
gap: file it on aether-lang-org/aether with a concrete spec (API shape,
call-site census, rationale) — that flow turns around same-day. Keep a
self-contained workaround in-tree meanwhile (the shim model).

- **#644 streaming HTTP request bodies — LANDED ≥0.347 (PR #990), NO consumer
  in zsync yet.** Filed as the future-blocker for a zsync *write/upload*
  receive-path (multi-MB uploads without buffering the whole body in RAM).
  zsync has no such path today — the servers (`fileserver`, `serverdsl`) are
  Range *download* servers, and `fileio.c`'s `pwrite` is the client's local
  scatter-write, not HTTP. So this closed as pure bookkeeping; wire it in only
  if/when an upload receiver is built. Then: handler dispatches at
  headers-complete once the body > 16 KiB; loop `http.request_body_read(req,
  off, 65536)` (blocks on the wire), terminate on
  `http.request_body_complete(req)`, `fileio`/`fs.pwrite` each window to disk.
  `http.request_body(req)` still works whole-body (materializes on demand) but
  DON'T mix it with `request_body_read` on the same req (returns ""). Chunked
  `Transfer-Encoding` request bodies remain unsupported upstream. Lineage:
  #626 asked, #642 shipped the API surface, #644 did the parse-loop reshape.
  (Still NOT landed: TLS-verify-skip toggle → `--no-check-certificate` stays a
  no-op.)

## What's NOT done (full list + priority in README "What is NOT yet ported")

Core protocol is complete + byte-verified + interop-tested both directions.
Outstanding = perf/polish, not algorithm: parallel range fetch (Go did 3
concurrent; we're sequential — highest value), HTTP/2, TLS-skip (needs
upstream client feature), proxy, per-host auth map, server-side 304 for
`-k`, `-u`-for-local-.zsync, random URL failover, old-file backup, mtime
restore on output, filename least-surprise check, live progress meter,
zsyncmake stdin. README has the prioritised list.

## Credit / license — do not muddy

Derived work. LICENSE untouched (Colin Phipps, Artistic-2.0). Ported `.ae`
keep original `SPDX-FileCopyrightText: ... Colin Phipps` headers. Port claims
no new copyright. Keep it that way.

## Stray

`.golangci.yaml` is leftover Go-lint config, now inert — fine to delete if
you're tidying (ask Paul first; it's harmless).
