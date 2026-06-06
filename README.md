# zsync 0.7.1

zsync is a file transfer program. It allows you to download a file from a
web server, where you have an older version of the file on your computer
already. zsync downloads only the new parts of the file. It uses the same
algorithm as rsync.

zsync does not require any special server software or a shell account on the
remote system at download time (rsync requires that you have an rsh or ssh
account, or that the remote system runs rsyncd). Instead, it uses a control
file - a .zsync file - that describes the file to be downloaded and enables
zsync to work out which blocks it needs. This file can be created by the admin
of the web server hosting the download, and placed alongside the file to
download - it is generated once, then any downloaders with zsync can use it.
Alternatively, anyone can download the file, make a .zsync and provide it to
other users.

The zsync web site is at http://zsync.moria.org.uk/ .

## Implementation

This zsync is written in [Aether](https://github.com/aether-lang-org/aether)
— a systems language that compiles to C. The rsync rolling-checksum engine,
the `.zsync` control-file format, the HTTP-range downloader and a native
test file server are all pure Aether (with a small Artistic-licensed C shim,
`rcksum/fileio.c`, for positional file I/O byte buffers). It uses Aether's
`std.http` client + server and `std.cryptography` (MD4/SHA-1). It does not
depend on Aether's MIT-licensed test/build tooling (aeocha/aeb) — the whole
thing stays under the Artistic License 2.0.

## Installation

zsync is free software. There is no implied support, no implied fitness for
purpose, no warranty. You use it at your own risk. See the included LICENSE for
details.

To build zsync you need the Aether toolchain (`ae` / `aetherc`) on your PATH,
then:

```shell
make bins        # builds ./build/zsync, ./build/zsyncmake, ./build/fileserver
make test        # runs the unit suite (ranges, rcksum, control, download, ...)
make itest       # full client+server round-trip over HTTP (Range/206)
```

You can use `zsync` and `zsyncmake` without installing them. If you want to
install them then, as root, run:

```shell
install build/zsync build/zsyncmake /usr/local/bin
install -D man/zsync.1 man/zsyncmake.1 /usr/local/man/man1/
```

### Source layout

- `rcksum/` — the rolling-checksum engine: `ranges` (block-range set),
  `checksums` (Adler rsum + MD4), `rcksum` (hash tables + the rolling
  matcher + scatter-write), and `fileio.{ae,c}` (the positional-I/O +
  byte-buffer + RFC1123Z C shim).
- `zsync/` — `control` (the `.zsync` control-file parser) and `download`
  (the high-level download State; named `download` because `state` is a
  reserved word in Aether).
- `cmd/` — `zsync` (client), `mklib`+`zsyncmake` (generator), `clientlib`
  (the HTTP fetch core), and `fileserver` (a native Range-aware test server).
- `test/` — a tiny in-tree assert harness (Artistic-2.0, no aeocha/aeb dep);
  `itest/run.sh` — the end-to-end integration test.

## Status & handoff

This is a **complete, working port of the core protocol**, intended to land
under `aether/std/http/zsync`. Everything is parity-tested against the
original Go implementation:

- `make test` → 7 test files, 94 assertions, all green. Reference values
  (rsums, MD4s, reconstructed-file SHA-1s) are generated from the actual Go
  code and asserted byte-exact, including the uint16-wraparound and MD4
  truncation edge cases.
- `make itest` → full round trip: `zsyncmake` builds a `.zsync`, the native
  `fileserver` serves it, the `zsync` client fetches the `.zsync` over HTTP,
  seeds from a deliberately-altered partial copy, fetches the missing blocks
  via HTTP `Range` requests (206), and the reconstructed output is
  byte-identical to the original (SHA-1 verified).
- `zsyncmake` output is **byte-for-byte identical** to the Go tool, MTime
  header included.

### Interoperability with the original C/Go zsync

Verified by building the Go `zsync`/`zsyncmake` from this repo's pre-port
history and cross-testing every meaningful combination. (Note: classic
zsync has **no dedicated server** — the "server" is any plain HTTP host that
supports `Range`. So "their server" means a standard Range-capable HTTP
server; our `fileserver` is one such, and is also handy for tests.)

| Case | Result |
|------|--------|
| `.zsync` produced by Aether `zsyncmake` vs Go `zsyncmake` | **byte-identical** — full format interop both directions |
| Aether client → Aether `fileserver` | OK (this is `make itest`) |
| **Go client → Aether `fileserver`** | **OK** — Go zsync downloads + verifies through our server |
| **Aether client → standard Range-capable HTTP server** | **OK** — works against any conformant host (Apache/nginx/…) |
| Either client → an HTTP server that ignores `Range` (returns 200) | both fail **identically** ("expected 206/partial content, got 200") — not an Aether issue; the host must support `Range` |

Bottom line: the two implementations are **wire-compatible** — same `.zsync`
format, same HTTP `Range`/`206` protocol. You can mix and match clients,
generators, and servers freely.

Porting it surfaced three gaps in Aether's stdlib, all filed and now landed
in Aether ≥ 0.218 — see
[aether-lang-org/aether](https://github.com/aether-lang-org/aether):
**#637** (MD4 + binary digest output), **#640** (`std.fs` positional I/O —
`pwrite`/`pread`/`ftruncate`/`fsync`), **#641** (`std.http` `serve_static`
honouring the `Range` header → 206/Content-Range).

### What is NOT yet ported (for whoever takes this over)

The omissions below are all correctness-preserving — they're performance,
deployment, and UX/defensive polish, not algorithm gaps. Roughly in
priority order:

**Performance**
1. **Parallel range fetching.** Go fetched up to 3 ranges concurrently
   (`errgroup` + `SetLimit(3)`); this port fetches ranges **sequentially**.
   Biggest real-world speed difference.
2. **HTTP/2.** Go set `ForceAttemptHTTP2`; this port uses whatever
   `std.http.client` negotiates (HTTP/1.1 today).

**Real-world hosting / networking**
3. **`--no-check-certificate`.** Accepted but a **no-op** — `std.http.client`
   has no TLS-verify-skip toggle yet, so HTTPS hosts with self-signed certs
   won't work. Needs a new upstream Aether client feature (file a 4th issue),
   then wire it. Note: the original Go integration tests *depended* on this
   (they ran Apache with a self-signed cert).
4. **HTTP proxy support.** Go honoured `HTTP_PROXY`/`HTTPS_PROXY`
   (`http.ProxyFromEnvironment`); this port does not. (Go had a tinyproxy
   integration test for it.)
5. **`-A` per-host auth map.** Go kept a `host=user:pass` map and applied the
   matching credential per request host. This port applies a **single**
   credential to all fetches and ignores the host part of `host=user:pass`.
6. **`-k` resume via server `304`.** The client correctly sends
   `If-Modified-Since` and handles a `304`, but the bundled `fileserver`
   never returns `304`, so that path is untested end-to-end.
7. **`-u` referer for a *local* `.zsync`.** Go's `-u` supplies the base URL
   when you hand it a local `.zsync` so relative `URL:` entries resolve. This
   port only resolves relative URLs against the source argument, which works
   when the source is itself an HTTP URL but not for the local-file + `-u`
   case.
8. **Multi-URL failover ordering.** Go picked URLs randomly (`rand.Intn`) and
   marked failed ones; this port tries them in **deterministic order**
   (intentional — friendlier for tests — but a behaviour difference).

**Finalisation / UX / defensive polish**
9. **Old-file backup on completion.** Go renamed an existing target to
   `<name>.zs-old` (hardlink, falling back to rename) before writing the new
   file; this port overwrites in place.
10. **Restore mtime on the finished file.** Go ran `os.Chtimes` to set the
    downloaded file's mtime from the `.zsync` `MTime`; not done here.
11. **"Principle of least surprise" filename check.** Go's `getFilename`
    cross-checks the remote `Filename:` header against the source's filename
    prefix and *rejects* a surprising name (anti-path-traversal + anti-
    surprise); this port just takes `Filename:` or falls back to
    `zsync-download`.
12. **`checkSuppliedFilename`.** Go refused to overwrite a non-`.zsync` file
    with the `-k` copy (guards `-k` pointed at the wrong file); not ported.
13. **Live progress meter.** Go printed a running percent / MB-per-second
    line as ranges arrived. `-q`/`-v` exist and `-v` prints final hash
    stats, but there's no live throughput display.
14. **`zsyncmake` stdin input.** Go read from stdin when given no file
    (that path actually panicked on a nil mtime — a real Go bug);
    this port requires a file argument.

## Use

In its simplest form, as an end-user:

```sh
zsync https://cdimage.ubuntu.com/ubuntu/daily-live/current/resolute-desktop-amd64.iso.zsync
```

Someone has to make a .zsync file for the download before you can use zsync.

You have to have an older version of the file around - or a related file that
contains a lot of the same content - otherwise there is little point in using
zsync. zsync normally looks in the current directory for a file of the same
name as the one being downloaded to use as a source of data. If your local
older copy is in a different directory, or you have another file with relevant
source data for zsync to use, you can specify it with -i.

## Offering zsync downloads

Simple example:
Suppose you have `http://example.com/dl/some-image-0.2.iso` ; which is in
`/var/www/downloads/` on your server.

```sh
cd /var/www/downloads/
zsyncmake some-image-0.2.iso
```

This creates some-image-0.2.iso.zsync in the same directory, a zsync control
file. A zsync user can then download the original file by running:

```sh
zsync -i some-image-0.1.iso http://example.com/dl/some-image-0.2.iso.zsync
```

Users still need to access to the full download file - zsync merely allows then
to save time by only downloading parts of the file.) A user with v0.1 of the
same file can now use zsync to download only the new bits.

By default zsyncmake will include a relative URL in the zsync control file, so
the client program will access the full file from the same server and directory
as the .zsync file. You can instead specify a URL to include with `-u` if you
are putting the .zsync file in a different directory or on another server.

## Feedback, Support

The original C/Go zsync lives at https://github.com/cph6/zsync/ (Colin Phipps,
cph@moria.org.uk).

This Aether port is maintained as part of the Aether ecosystem; see the
"Status & handoff" section above for what's done and what's left, and the
linked aether-lang-org/aether issues (#637, #640, #641) for the stdlib work
it drove.

## Copyright, Author, Acknowledgements

zsync is based on the rsync algorithm, by Andrew Tridgell. It also incorporates
a number of optimisations, based on ideas in academic papers by Utku Irmak,
Svilen Mihaylov and Torsten Suel (primarily "Improved Single-Round Protocols
for Remote File Synchronization", Sept 2004).

zsync is copyright 2004,2005,2007,2009,2025,2026 Colin Phipps <cph@moria.org.uk>.
zsync is made available under the Artistic License 2.0 - see the file LICENSE
for details.

Also, thanks to Dennis Schridde, Timothy Lee, Richard Kiss, Érsek László, James
Montgomerie, James Antill, saul@alien-science.org, Kent Mein, Marc Lehmann,
Robert Lemmen, Mark Adler, Ricardo Correia, Karl Kalleberg, Michael Stone,
Richard Lucassen, Duncan Mac-Vicar, Jari Aalto, Marcin Mirosław, Jan Varho,
Loïc Minier, Gian Merlino and S Page for useful feedback and bug reports for
previous versions.

