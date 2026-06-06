#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <setjmp.h>
#include "aether_panic.h"
#include "aether_stringseq.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>      // _setmode, _fileno
#include <fcntl.h>   // _O_BINARY
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#else
#include <unistd.h>
#include <sched.h>
#endif
#ifdef _WIN32
#  define aether_aligned_alloc(align, size) _aligned_malloc((size), (align))
#else
#  define aether_aligned_alloc(align, size) aligned_alloc((align), (size))
#endif
#ifndef likely
#  if defined(__GNUC__) || defined(__clang__)
#    define likely(x)   __builtin_expect(!!(x), 1)
#    define unlikely(x) __builtin_expect(!!(x), 0)
#  else
#    define likely(x)   (x)
#    define unlikely(x) (x)
#  endif
#endif
#ifndef AETHER_GCC_COMPAT
#  if (defined(__GNUC__) || defined(__clang__)) && !defined(__EMSCRIPTEN__)
#    define AETHER_GCC_COMPAT 1
#  else
#    define AETHER_GCC_COMPAT 0
#  endif
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#ifdef _WIN32
static inline int64_t _aether_clock_ns(void) {
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (int64_t)((double)now.QuadPart / freq.QuadPart * 1000000000.0);
}
#elif defined(__EMSCRIPTEN__)
static inline int64_t _aether_clock_ns(void) {
    return (int64_t)(emscripten_get_now() * 1000000.0);
}
#elif defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0)
static inline int64_t _aether_clock_ns(void) { return 0; }
#else
static inline int64_t _aether_clock_ns(void) {
    struct timespec _ts;
    clock_gettime(CLOCK_MONOTONIC, &_ts);
    return (int64_t)_ts.tv_sec * 1000000000LL + _ts.tv_nsec;
}
#endif
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
extern void* aether_caps_malloc(size_t bytes);
static inline const char* aether_uniform_heap_str(const char* s, int is_heap) {
    if (!s) return (const char*)0;
    if (is_heap) return s;
    /* AetherString-aware length probe — see is_aether_string in
     * std/string/aether_string.h. Byte-by-byte to stay ASan-clean
     * on short literal allocations (e.g. "x"). */
    const unsigned char* _p = (const unsigned char*)s;
    const char* _data = s;
    size_t _n;
    if (_p[0] == 0xDE && _p[1] == 0xC0 && _p[2] == 0x57 && _p[3] == 0xAE) {
        /* Struct layout: magic(u32), ref_count(i32), length(size_t),
         * capacity(size_t), data(char*). Read length and data via
         * a typed view — the struct's data pointer is what we copy. */
        struct _AeStrHdr { unsigned int magic; int ref_count; size_t length; size_t capacity; char* data; };
        const struct _AeStrHdr* _h = (const struct _AeStrHdr*)s;
        _n = _h->length;
        _data = _h->data ? _h->data : s;
    } else {
        _n = strlen(s);
    }
    char* _d = (char*)aether_caps_malloc(_n + 1);
    if (!_d) return (const char*)0;
    if (_n) memcpy(_d, _data, _n);
    _d[_n] = '\0';
    return (const char*)_d;
}
extern void string_release(const char*);
static inline void aether_heap_str_free(const char* s) {
    if (!s) return;
    const unsigned char* _hp = (const unsigned char*)s;
    if (_hp[0] == 0xDE && _hp[1] == 0xC0 && _hp[2] == 0x57 && _hp[3] == 0xAE) {
        string_release(s);
    } else {
        free((void*)s);
    }
}
#include <stdarg.h>
static void* _aether_interp(const char* fmt, ...) {
    va_list args, args2;
    va_start(args, fmt);
    va_copy(args2, args);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    char* str = (char*)malloc(len + 1);
    vsnprintf(str, len + 1, fmt, args2);
    va_end(args2);
    return (void*)str;
}
extern const char* aether_string_data(const void* s);
extern size_t aether_string_length(const void* s);
extern void* string_new_with_length(const char* data, int length);
extern int list_add_string_owned(void* list, void* item);
extern int map_put_string_owned(void* map, const char* key, void* value);
static inline const char* _aether_safe_str(const void* s) {
    if (!s) return "(null)";
    return aether_string_data(s);
}
static inline const char* _aether_duration_repr(int64_t ns) {
    static char _buf[64];
    int64_t abs_ns = ns < 0 ? -ns : ns;
    struct _du { const char* suffix; int64_t scale; } units[] = {
        {"d", 86400000000000LL}, {"h", 3600000000000LL},
        {"m", 60000000000LL}, {"s", 1000000000LL},
        {"ms", 1000000LL}, {"us", 1000LL}, {"ns", 1LL}
    };
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        if (abs_ns >= units[i].scale || units[i].scale == 1) {
            if (ns % units[i].scale == 0) {
                snprintf(_buf, sizeof(_buf), "%lld%s", (long long)(ns / units[i].scale), units[i].suffix);
            } else {
                double v = (double)ns / (double)units[i].scale;
                snprintf(_buf, sizeof(_buf), "%.9g%s", v, units[i].suffix);
            }
            return _buf;
        }
    }
    return "0ns";
}
extern void aether_sleep_ms(int ms);
#if !AETHER_GCC_COMPAT
static void* _aether_ref_new(intptr_t val) { intptr_t* r = malloc(sizeof(intptr_t)); *r = val; return (void*)r; }
#endif
typedef struct { void (*fn)(void); void* env; } _AeClosure;
static inline void* _aether_box_closure(_AeClosure c) { _AeClosure* p = malloc(sizeof(_AeClosure)); *p = c; return (void*)p; }
static inline _AeClosure _aether_unbox_closure(void* p) { return *(_AeClosure*)p; }
typedef struct { _AeClosure compute; intptr_t value; int evaluated; } _AeThunk;
static inline void* _aether_thunk_new(_AeClosure c) { _AeThunk* t = malloc(sizeof(_AeThunk)); t->compute = c; t->value = 0; t->evaluated = 0; return (void*)t; }
static inline intptr_t _aether_thunk_force(void* p) { _AeThunk* t = (_AeThunk*)p; if (!t->evaluated) { t->value = (intptr_t)((intptr_t(*)(void*))t->compute.fn)(t->compute.env); t->evaluated = 1; } return t->value; }
static inline void _aether_thunk_free(void* p) { if (p) free(p); }
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1) && !defined(__arm__) && !defined(__thumb__)
#include <termios.h>
static struct termios _aether_orig_termios;
static void _aether_raw_mode(void) {
    tcgetattr(0, &_aether_orig_termios);
    struct termios raw = _aether_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &raw);
}
static void _aether_cooked_mode(void) {
    tcsetattr(0, TCSANOW, &_aether_orig_termios);
}
#else
static void _aether_raw_mode(void) {}
static void _aether_cooked_mode(void) {}
#endif
static void* _aether_ctx_stack[64];
static int _aether_ctx_depth = 0;
static inline void _aether_ctx_push(void* ctx) { if (_aether_ctx_depth < 64) _aether_ctx_stack[_aether_ctx_depth++] = ctx; }
static inline void _aether_ctx_pop(void) { if (_aether_ctx_depth > 0) _aether_ctx_depth--; }
static inline void* _aether_ctx_get(void) { return _aether_ctx_depth > 0 ? _aether_ctx_stack[_aether_ctx_depth-1] : (void*)0; }

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

void aether_args_init(int argc, char** argv);


typedef struct { const char* _0; int _1; } _tuple_string_int;

typedef struct Harness Harness;
typedef struct Harness {
    int passed;
    int failed;
} Harness;

// Forward declarations
static int fileio_open_rw(const char*);
static int fileio_write_at(int, const char*, int, int);
static _tuple_string_int fileio_read_at(int, int, int);
static int fileio_truncate_to(int, int);
static int fileio_close_fd(int);
static int fileio_sync_fd(int);
static Harness* assert_new(void);
static void assert_ok(Harness*, int, const char*);
static void assert_eq_int(Harness*, int, int, const char*);
static void assert_eq_str(Harness*, const char*, const char*, const char*);
static void assert_report(Harness*);

// Extern C function: string_new
void* string_new(const char*);

// Extern C function: string_from_cstr
void* string_from_cstr(const char*);

// Extern C function: string_from_literal
void* string_from_literal(const char*);

// Extern C function: string_new_with_length
void* string_new_with_length(const char*, int);

// Extern C function: string_empty
void* string_empty(void);

// Extern C function: string_retain
void string_retain(const char*);

// Extern C function: string_release
void string_release(const char*);

// Extern C function: string_free
void string_free(const char*);

// Extern C function: string_concat
const char* string_concat(const char*, const char*);

// Extern C function: string_concat_wrapped
void* string_concat_wrapped(const char*, const char*);

// Extern C function: string_length
int string_length(const char*);

// Extern C function: string_char_at
int string_char_at(const char*, int);

// Extern C function: string_equals
int string_equals(const char*, const char*);

// Extern C function: string_compare
int string_compare(const char*, const char*);

// Extern C function: string_starts_with
int string_starts_with(const char*, const char*);

// Extern C function: string_ends_with
int string_ends_with(const char*, const char*);

// Extern C function: string_contains
int string_contains(const char*, const char*);

// Extern C function: string_index_of
int string_index_of(const char*, const char*);

// Extern C function: string_index_of_from
int string_index_of_from(const char*, const char*, int);

// Extern C function: string_substring
const char* string_substring(const char*, int, int);

// Extern C function: string_substring_n
const char* string_substring_n(const char*, int, int, int);

// Extern C function: string_length_n
int string_length_n(const char*, int);

// Extern C function: string_char_at_n
int string_char_at_n(const char*, int, int);

// Extern C function: string_index_of_from_n
int string_index_of_from_n(const char*, int, const char*, int);

// Extern C function: string_from_char
void* string_from_char(int);

// Extern C function: string_to_upper
const char* string_to_upper(const char*);

// Extern C function: string_to_lower
const char* string_to_lower(const char*);

// Extern C function: string_trim
const char* string_trim(const char*);

// Extern C function: string_split
void* string_split(const char*, const char*);

// Extern C function: string_array_size
int string_array_size(void*);

// Extern C function: string_array_get
void* string_array_get(void*, int);

// Extern C function: string_array_free
void string_array_free(void*);

// Extern C function: string_split_to_seq
StringSeq* string_split_to_seq(const char*, const char*);

// Extern C function: string_seq_empty
StringSeq* string_seq_empty(void);

// Extern C function: string_seq_cons
StringSeq* string_seq_cons(const char*, StringSeq*);

// Extern C function: string_seq_head
const char* string_seq_head(StringSeq*);

// Extern C function: string_seq_tail
StringSeq* string_seq_tail(StringSeq*);

// Extern C function: string_seq_is_empty
int string_seq_is_empty(StringSeq*);

// Extern C function: string_seq_length
int string_seq_length(StringSeq*);

// Extern C function: string_seq_retain
StringSeq* string_seq_retain(StringSeq*);

// Extern C function: string_seq_free
void string_seq_free(StringSeq*);

// Extern C function: string_seq_from_array
StringSeq* string_seq_from_array(void*, int);

// Extern C function: string_seq_to_array
void* string_seq_to_array(StringSeq*);

// Extern C function: string_seq_reverse
StringSeq* string_seq_reverse(StringSeq*);

// Extern C function: string_seq_concat
StringSeq* string_seq_concat(StringSeq*, StringSeq*);

// Extern C function: string_seq_take
StringSeq* string_seq_take(StringSeq*, int);

// Extern C function: string_seq_drop
StringSeq* string_seq_drop(StringSeq*, int);

// Extern C function: string_to_cstr
const char* string_to_cstr(const char*);

// Extern C function: string_from_int
void* string_from_int(int);

// Extern C function: string_from_long
void* string_from_long(int64_t);

// Extern C function: string_from_float
void* string_from_float(double);

// Extern C function: string_from_int_radix
void* string_from_int_radix(int64_t, int);

// Extern C function: string_pad_start
void* string_pad_start(const char*, int, int);

// Extern C function: string_pad_end
void* string_pad_end(const char*, int, int);

// Extern C function: string_to_int_raw
int string_to_int_raw(const char*, void*);

// Extern C function: string_to_long_raw
int string_to_long_raw(const char*, void*);

// Extern C function: string_to_float_raw
int string_to_float_raw(const char*, void*);

// Extern C function: string_to_double_raw
int string_to_double_raw(const char*, void*);

// Extern C function: string_to_int_radix_raw
int string_to_int_radix_raw(const char*, int, void*);

// Extern C function: string_try_int
int string_try_int(const char*);

// Extern C function: string_get_int
int string_get_int(const char*);

// Extern C function: string_try_long
int string_try_long(const char*);

// Extern C function: string_get_long
int64_t string_get_long(const char*);

// Extern C function: string_try_float
int string_try_float(const char*);

// Extern C function: string_get_float
double string_get_float(const char*);

// Extern C function: string_try_double
int string_try_double(const char*);

// Extern C function: string_get_double
double string_get_double(const char*);

// Extern C function: string_try_int_radix
int string_try_int_radix(const char*, int);

// Extern C function: string_get_int_radix
int64_t string_get_int_radix(const char*, int);

// Extern C function: string_format_list
void* string_format_list(const char*, void*);

// Extern C function: string_glob_match_raw
int string_glob_match_raw(const char*, const char*, int);

// Extern C function: malloc (libc-provided, declaration skipped)
// Extern C function: zsync_io_open_rw_trunc
int zsync_io_open_rw_trunc(const char*);

// Extern C function: zsync_io_open_ro
int zsync_io_open_ro(const char*);

// Extern C function: zsync_io_pwrite
int64_t zsync_io_pwrite(int, const char*, int64_t, int64_t);

// Extern C function: zsync_io_pread_alloc
const char* zsync_io_pread_alloc(int, int64_t, int64_t);

// Extern C function: zsync_io_last_read_len
int64_t zsync_io_last_read_len(void);

// Extern C function: zsync_io_ftruncate
int zsync_io_ftruncate(int, int64_t);

// Extern C function: zsync_io_close
int zsync_io_close(int);

// Extern C function: zsync_io_fsync
int zsync_io_fsync(int);

// Extern C function: zsync_buf_alloc
void* zsync_buf_alloc(int64_t);

// Extern C function: zsync_buf_alloc_str
const char* zsync_buf_alloc_str(int64_t);

// Extern C function: zsync_buf_get
int zsync_buf_get(void*, int64_t);

// Extern C function: zsync_buf_set
void zsync_buf_set(void*, int64_t, int);

// Extern C function: zsync_buf_or
void zsync_buf_or(void*, int64_t, int);

// Extern C function: zsync_buf_free
void zsync_buf_free(void*);

// Extern C function: zsync_buf_identity
const char* zsync_buf_identity(void*);

// Extern C function: zsync_rfc1123z
const char* zsync_rfc1123z(int64_t);

// Extern C function: zsync_parse_rfc1123
int64_t zsync_parse_rfc1123(const char*);

// Extern C function: zsync_dup16
const char* zsync_dup16(void*);

// Extern C function: malloc (libc-provided, declaration skipped)

// Import: std.string
// Import: rcksum.fileio
// Import: test.assert
#line 29 "rcksum/fileio.ae"
static int fileio_open_rw(const char* path) {
#line 30 "rcksum/fileio.ae"
    return zsync_io_open_rw_trunc(aether_string_data(path));
}

#line 42 "rcksum/fileio.ae"
static int fileio_write_at(int fd, const char* data, int len, int offset) {
#line 43 "rcksum/fileio.ae"
int64_t written = zsync_io_pwrite(fd, aether_string_data(data), len, offset);
#line 44 "rcksum/fileio.ae"
    return written;
}

#line 50 "rcksum/fileio.ae"
static _tuple_string_int fileio_read_at(int fd, int len, int offset) {
    int _heap_buf = 0; (void)_heap_buf;
    const char* buf = NULL;
    int _heap_s = 0; (void)_heap_s;
    const char* s = NULL;
#line 51 "rcksum/fileio.ae"
{ const char* _tmp_old = buf; buf = zsync_io_pread_alloc(fd, len, offset); if (_heap_buf) aether_heap_str_free(_tmp_old); _heap_buf = 0; }
#line 52 "rcksum/fileio.ae"
int64_t n = zsync_io_last_read_len();
if (n < 0) {
        {
#line 54 "rcksum/fileio.ae"
            _tuple_string_int _builder_ret = (_tuple_string_int){aether_uniform_heap_str((const char*)(""), 0), (-(1))};
            /* deferred */ if (_heap_buf) { aether_heap_str_free(buf); buf = NULL; _heap_buf = 0; }
            return _builder_ret;
        }
    }
#line 58 "rcksum/fileio.ae"
{ const char* _tmp_old = s; s = string_new_with_length(buf, n); if (_heap_s) aether_heap_str_free(_tmp_old); _heap_s = 1; }
#line 59 "rcksum/fileio.ae"
    _tuple_string_int _builder_ret = (_tuple_string_int){aether_uniform_heap_str((const char*)(s), _heap_s), n};
    /* deferred */ if (_heap_buf) { aether_heap_str_free(buf); buf = NULL; _heap_buf = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_buf) { aether_heap_str_free(buf); buf = NULL; _heap_buf = 0; }
}

#line 62 "rcksum/fileio.ae"
static int fileio_truncate_to(int fd, int length) {
#line 63 "rcksum/fileio.ae"
    return zsync_io_ftruncate(fd, length);
}

#line 66 "rcksum/fileio.ae"
static int fileio_close_fd(int fd) {
#line 67 "rcksum/fileio.ae"
    return zsync_io_close(fd);
}

#line 70 "rcksum/fileio.ae"
static int fileio_sync_fd(int fd) {
#line 71 "rcksum/fileio.ae"
    return zsync_io_fsync(fd);
}

#line 25 "test/assert.ae"
static Harness* assert_new(void) {
#line 26 "test/assert.ae"
void* raw = malloc(16);
#line 27 "test/assert.ae"
Harness* h = ((Harness*)(raw));
#line 28 "test/assert.ae"
(h->passed = 0);
#line 29 "test/assert.ae"
(h->failed = 0);
#line 30 "test/assert.ae"
    return h;
}

#line 33 "test/assert.ae"
static void assert_ok(Harness* h, int cond, const char* name) {
if (cond != 0) {
        {
#line 35 "test/assert.ae"
(h->passed = (h->passed + 1));
#line 36 "test/assert.ae"
printf("  ok   %s", _aether_safe_str(name)); putchar('\n');
        }
    } else {
        {
#line 38 "test/assert.ae"
(h->failed = (h->failed + 1));
#line 39 "test/assert.ae"
printf("  FAIL %s", _aether_safe_str(name)); putchar('\n');
        }
    }
}

#line 43 "test/assert.ae"
static void assert_eq_int(Harness* h, int got, int want, const char* name) {
if (got == want) {
        {
#line 45 "test/assert.ae"
(h->passed = (h->passed + 1));
#line 46 "test/assert.ae"
printf("  ok   %s", _aether_safe_str(name)); putchar('\n');
        }
    } else {
        {
#line 48 "test/assert.ae"
(h->failed = (h->failed + 1));
#line 49 "test/assert.ae"
printf("  FAIL %s: got %d, want %d", _aether_safe_str(name), got, want); putchar('\n');
        }
    }
}

#line 53 "test/assert.ae"
static void assert_eq_str(Harness* h, const char* got, const char* want, const char* name) {
if (string_equals(got, want) == 1) {
        {
#line 55 "test/assert.ae"
(h->passed = (h->passed + 1));
#line 56 "test/assert.ae"
printf("  ok   %s", _aether_safe_str(name)); putchar('\n');
        }
    } else {
        {
#line 58 "test/assert.ae"
(h->failed = (h->failed + 1));
#line 59 "test/assert.ae"
printf("  FAIL %s: got '%s', want '%s'", _aether_safe_str(name), _aether_safe_str(got), _aether_safe_str(want)); putchar('\n');
        }
    }
}

#line 64 "test/assert.ae"
static void assert_report(Harness* h) {
#line 65 "test/assert.ae"
int total = (h->passed + h->failed);
#line 66 "test/assert.ae"
puts("");
#line 67 "test/assert.ae"
printf("%d/%d passed, %d failed", h->passed, total, h->failed); putchar('\n');
if (h->failed != 0) {
        {
#line 69 "test/assert.ae"
exit(1);
        }
    }
}

int main(int argc, char** argv) {
    #ifdef _WIN32
    SetConsoleOutputCP(65001);  // CP_UTF8
    SetConsoleCP(65001);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    #endif
    aether_args_init(argc, argv);
    
    int _heap_path = 0; (void)_heap_path;
    const char* path = NULL;
    int _heap_s = 0; (void)_heap_s;
    const char* s = NULL;
    int _heap_mid = 0; (void)_heap_mid;
    const char* mid = NULL;
    int _heap_t = 0; (void)_heap_t;
    const char* t = NULL;
    int _heap_b = 0; (void)_heap_b;
    const char* b = NULL;
    {
#line 13 "rcksum/fileio_test.ae"
puts("=== fileio (positional pwrite/pread) ===");
#line 14 "rcksum/fileio_test.ae"
Harness* h = assert_new();
#line 16 "rcksum/fileio_test.ae"
{ const char* _tmp_old = path; path = "/tmp/zsync_fileio_check.bin"; if (_heap_path) aether_heap_str_free(_tmp_old); _heap_path = 0; }
#line 17 "rcksum/fileio_test.ae"
int fd = fileio_open_rw(path);
#line 18 "rcksum/fileio_test.ae"
assert_ok(h, (fd >= 0), "open_rw returns valid fd");
#line 21 "rcksum/fileio_test.ae"
int n1 = fileio_write_at(fd, "world", 5, 5);
#line 22 "rcksum/fileio_test.ae"
int n2 = fileio_write_at(fd, "hello", 5, 0);
#line 23 "rcksum/fileio_test.ae"
assert_eq_int(h, n1, 5, "pwrite world -> 5");
#line 24 "rcksum/fileio_test.ae"
assert_eq_int(h, n2, 5, "pwrite hello -> 5");
#line 27 "rcksum/fileio_test.ae"
        _tuple_string_int _tup0 = fileio_read_at(fd, 10, 0);
        { const char* _tmp_old = s; s = _tup0._0; if (_heap_s) aether_heap_str_free(_tmp_old); _heap_s = 1; }
        int rn = _tup0._1;
#line 28 "rcksum/fileio_test.ae"
assert_eq_int(h, rn, 10, "pread 10 bytes");
#line 29 "rcksum/fileio_test.ae"
assert_eq_str(h, s, "helloworld", "reassembled out-of-order writes");
#line 32 "rcksum/fileio_test.ae"
        _tuple_string_int _tup1 = fileio_read_at(fd, 5, 2);
        { const char* _tmp_old = mid; mid = _tup1._0; if (_heap_mid) aether_heap_str_free(_tmp_old); _heap_mid = 1; }
        int mn = _tup1._1;
#line 33 "rcksum/fileio_test.ae"
assert_eq_int(h, mn, 5, "pread mid 5");
#line 34 "rcksum/fileio_test.ae"
assert_eq_str(h, mid, "llowo", "mid slice");
#line 37 "rcksum/fileio_test.ae"
fileio_truncate_to(fd, 5);
#line 38 "rcksum/fileio_test.ae"
        _tuple_string_int _tup2 = fileio_read_at(fd, 100, 0);
        { const char* _tmp_old = t; t = _tup2._0; if (_heap_t) aether_heap_str_free(_tmp_old); _heap_t = 1; }
        int tn = _tup2._1;
#line 39 "rcksum/fileio_test.ae"
assert_eq_int(h, tn, 5, "after truncate, only 5 bytes");
#line 40 "rcksum/fileio_test.ae"
assert_eq_str(h, t, "hello", "truncated content");
#line 45 "rcksum/fileio_test.ae"
fileio_write_at(fd, "a", 1, 0);
#line 46 "rcksum/fileio_test.ae"
({ const char* _ad_0 = (const char*)(string_from_char(0)); int _ad_r = fileio_write_at(fd, _ad_0, 1, 1); aether_heap_str_free(_ad_0); _ad_r; });
#line 47 "rcksum/fileio_test.ae"
fileio_write_at(fd, "b", 1, 2);
#line 48 "rcksum/fileio_test.ae"
        _tuple_string_int _tup3 = fileio_read_at(fd, 3, 0);
        { const char* _tmp_old = b; b = _tup3._0; if (_heap_b) aether_heap_str_free(_tmp_old); _heap_b = 1; }
        int bn = _tup3._1;
#line 49 "rcksum/fileio_test.ae"
assert_eq_int(h, bn, 3, "binary read 3");
#line 50 "rcksum/fileio_test.ae"
assert_eq_int(h, (string_char_at_n(b, 3, 0) & 0xff), 97, "byte0 'a'");
#line 51 "rcksum/fileio_test.ae"
assert_eq_int(h, (string_char_at_n(b, 3, 1) & 0xff), 0, "byte1 NUL preserved");
#line 52 "rcksum/fileio_test.ae"
assert_eq_int(h, (string_char_at_n(b, 3, 2) & 0xff), 98, "byte2 'b'");
#line 54 "rcksum/fileio_test.ae"
fileio_close_fd(fd);
#line 55 "rcksum/fileio_test.ae"
assert_report(h);
    }
    /* deferred */ if (_heap_b) { aether_heap_str_free(b); b = NULL; _heap_b = 0; }
    /* deferred */ if (_heap_t) { aether_heap_str_free(t); t = NULL; _heap_t = 0; }
    /* deferred */ if (_heap_mid) { aether_heap_str_free(mid); mid = NULL; _heap_mid = 0; }
    /* deferred */ if (_heap_s) { aether_heap_str_free(s); s = NULL; _heap_s = 0; }
    /* deferred */ if (_heap_path) { aether_heap_str_free(path); path = NULL; _heap_path = 0; }
    return 0;
}
