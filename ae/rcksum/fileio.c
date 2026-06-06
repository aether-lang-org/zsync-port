/*
 * SPDX-FileCopyrightText: 2026 Colin Phipps <cph@moria.org.uk>
 * SPDX-License-Identifier: Artistic-2.0
 *
 * Positional file I/O shim for the Aether zsync port.
 *
 * std.fs has only whole-file / sequential read+write today, but zsync
 * scatters reconstructed blocks into the output file at arbitrary
 * offsets as they are matched (pwrite) and reads them back (pread).
 * Rather than `extern` libc's pwrite/pread directly — which clashes
 * with glibc's size_t/off_t prototypes when Aether emits its own — we
 * expose uniquely-named wrappers here and extern those from Aether.
 *
 * Uniquely named (zsync_io_*) so there is zero chance of colliding with
 * a libc or stdlib prototype. A stdlib-wish has been filed to add
 * positional I/O to std.fs; this shim is the self-contained stopgap.
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>

/* Open for read+write, create if absent, truncate. mode 0644.
 * Returns the fd, or -1 on failure. */
int zsync_io_open_rw_trunc(const char *path) {
    return open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
}

/* Open an existing file read-only. Returns fd or -1. */
int zsync_io_open_ro(const char *path) {
    return open(path, O_RDONLY);
}

/* Write `count` bytes of `buf` at absolute `offset`. Returns bytes
 * written, or -1. Loops to handle short writes. */
long zsync_io_pwrite(int fd, const char *buf, long count, long offset) {
    long total = 0;
    while (total < count) {
        ssize_t n = pwrite(fd, buf + total, (size_t)(count - total),
                           (off_t)(offset + total));
        if (n < 0) return -1;
        if (n == 0) break;
        total += n;
    }
    return total;
}

/* Read up to `count` bytes at absolute `offset` into a freshly
 * malloc'd buffer (count+1 bytes, NUL-terminated for str-safety).
 * Returns the buffer; the caller reads the actual byte count via
 * zsync_io_last_read_len() and must free it. Returns NULL on error.
 *
 * Returning the buffer (rather than reading into a caller buffer)
 * sidesteps Aether's ptr-vs-string param typing for the read path. */
static long zsync_io_last_n = 0;

char *zsync_io_pread_alloc(int fd, long count, long offset) {
    char *buf = (char *)malloc((size_t)count + 1);
    if (!buf) { zsync_io_last_n = -1; return NULL; }
    long total = 0;
    while (total < count) {
        ssize_t n = pread(fd, buf + total, (size_t)(count - total),
                          (off_t)(offset + total));
        if (n < 0) { free(buf); zsync_io_last_n = -1; return NULL; }
        if (n == 0) break;   /* EOF */
        total += n;
    }
    buf[total] = '\0';
    zsync_io_last_n = total;
    return buf;
}

long zsync_io_last_read_len(void) {
    return zsync_io_last_n;
}

/* Truncate the file to `length` bytes. Returns 0 on success, -1. */
int zsync_io_ftruncate(int fd, long length) {
    return ftruncate(fd, (off_t)length);
}

int zsync_io_close(int fd) {
    return close(fd);
}

/* fsync to flush before close/rename. Returns 0 / -1. */
int zsync_io_fsync(int fd) {
    return fsync(fd);
}
