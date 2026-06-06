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

- `rcksum/` — the rolling-checksum engine (ranges, checksums, the matcher)
  and the positional-I/O shim.
- `zsync/` — the `.zsync` control-file parser and the download State.
- `cmd/` — the `zsync` client, `zsyncmake` generator, and a native `fileserver`.
- `test/` — a tiny in-tree assert harness; `itest/` — the integration test.

### Known gaps vs. the historical C/Go zsync

The core protocol is complete and verified byte-for-byte. A few CLI niceties
are accepted but not yet fully wired: `--no-check-certificate` is a no-op
(the HTTP client has no TLS-verify-skip toggle yet), `-A` applies a single
credential to all hosts rather than a per-host map, and `If-Modified-Since`
relies on the server returning `304` (the bundled test `fileserver` does not).

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

zsync is not very actively developed in recent years, but you can submit pull
requests or issues on Github https://github.com/cph6/zsync/. Or you can reach
out to me personally at cph@moria.org.uk.

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

