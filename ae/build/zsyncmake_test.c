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


typedef struct { const char* _0; int _1; const char* _2; } _tuple_string_int_string;
typedef struct { int _0; int _1; const char* _2; } _tuple_int_int_string;
typedef struct { int _0; const char* _1; } _tuple_int_string;
typedef struct { int _0; int _1; int _2; } _tuple_int_int_int;
typedef struct { const char* _0; int _1; } _tuple_string_int;
typedef struct { const char* _0; const char* _1; } _tuple_string_string;
typedef struct { const char* _0; const char* _1; int _2; } _tuple_string_string_int;

typedef struct Harness Harness;
typedef struct LocalTime LocalTime;
typedef struct RSum RSum;
typedef struct Harness {
    int passed;
    int failed;
} Harness;

typedef struct LocalTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int nanos;
    int tz_offset_minutes;
} LocalTime;

typedef struct RSum {
    int a;
    int b;
} RSum;

#define fs_KIND_OK (0)
#define fs_KIND_NOT_FOUND (1)
#define fs_KIND_PERMISSION_DENIED (2)
#define fs_KIND_EXISTS (3)
#define fs_KIND_CROSS_DEVICE (4)
#define fs_KIND_IO (5)
#define fs_KIND_INVALID (6)
#define fs_KIND_LOOP (7)
#define fs_KIND_NAME_TOO_LONG (8)
#define fs_KIND_NO_SPACE (9)
#define fs_KIND_IS_DIR (10)
#define fs_KIND_NOT_DIR (11)
#define fs_KIND_UNAVAILABLE (99)
// Forward declarations
static _tuple_int_string fs_mtime(const char*);
static _tuple_string_int_string fs_read_binary(const char*);
static int mklib_f2i(double);
static double mklib_i2f(int);
static const char* mklib_VERSION(void);
static _tuple_int_int_int mklib_determine_hash_lengths(int, int);
static double mklib_log2f(double);
static _tuple_string_int mklib_generate_n(const char*, int, int, const char*, const char*, const char*);
static const char* mklib_build_header(int, int, int, int, int, const char*, const char*, const char*);
static const char* mklib_generate(const char*, int, int, const char*, const char*, const char*);
static const char* mklib_block_at(const char*, int, int, int);
static const char* mklib_cat(const char*, const char*);
static const char* mklib_int_str(int);
static Harness* assert_new(void);
static void assert_ok(Harness*, int, const char*);
static void assert_eq_int(Harness*, int, int, const char*);
static void assert_eq_str(Harness*, const char*, const char*, const char*);
static void assert_report(Harness*);
static _tuple_string_string cryptography_sha1_hex(const char*, int);
static _tuple_string_int_string cryptography_md4_bytes(const char*, int);
static RSum* checksums_new_rsum(int, int);
static int checksums_rsum_a(RSum*);
static int checksums_rsum_b(RSum*);
static RSum* checksums_calc_rsum_block(const char*, int, int, int);
static const char* checksums_calc_checksum(const char*, int);
static int checksums_byte_at(const char*, int, int);
static void* fileio_buf_alloc(int);
static void fileio_buf_set(void*, int, int);
static const char* fileio_buf_to_str(void*, int);
static const char* fileio_pad_block(const char*, int, int, int, int);
static const char* fileio_slice(const char*, int, int, int);
static const char* fileio_buf_as_string(void*);
_tuple_string_string_int read_line(const char*);

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

// Extern C function: file_open_raw
void* file_open_raw(const char*, const char*);

// Extern C function: file_close
int file_close(void*);

// Extern C function: file_read_all_raw
const char* file_read_all_raw(void*);

// Extern C function: file_write_raw
int file_write_raw(void*, const char*, int);

// Extern C function: file_exists
int file_exists(const char*);

// Extern C function: fs_path_exists
int fs_path_exists(const char*);

// Extern C function: file_delete_raw
int file_delete_raw(const char*);

// Extern C function: file_size_raw
int file_size_raw(const char*);

// Extern C function: file_mtime
int file_mtime(const char*);

// Extern C function: file_mtime_raw
int file_mtime_raw(const char*);

// Extern C function: dir_exists
int dir_exists(const char*);

// Extern C function: dir_create_raw
int dir_create_raw(const char*);

// Extern C function: dir_create_mode_raw
int dir_create_mode_raw(const char*, int);

// Extern C function: dir_delete_raw
int dir_delete_raw(const char*);

// Extern C function: dir_list_raw
void* dir_list_raw(const char*);

// Extern C function: fs_mkdir_p_raw
int fs_mkdir_p_raw(const char*);

// Extern C function: fs_symlink_raw
int fs_symlink_raw(const char*, const char*);

// Extern C function: fs_readlink_raw
const char* fs_readlink_raw(const char*);

// Extern C function: fs_is_symlink
int fs_is_symlink(const char*);

// Extern C function: fs_unlink_raw
int fs_unlink_raw(const char*);

// Extern C function: fs_write_binary_raw
int fs_write_binary_raw(const char*, const char*, int);

// Extern C function: fs_write_atomic_raw
int fs_write_atomic_raw(const char*, const char*, int);

// Extern C function: fs_rename_raw
int fs_rename_raw(const char*, const char*);

// Extern C function: fs_try_stat
int fs_try_stat(const char*);

// Extern C function: fs_get_stat_kind
int fs_get_stat_kind(void);

// Extern C function: fs_get_stat_size
int fs_get_stat_size(void);

// Extern C function: fs_get_stat_mtime
int fs_get_stat_mtime(void);

// Extern C function: fs_try_read_binary
int fs_try_read_binary(const char*);

// Extern C function: fs_get_read_binary
const char* fs_get_read_binary(void);

// Extern C function: fs_get_read_binary_length
int fs_get_read_binary_length(void);

// Extern C function: fs_release_read_binary
void fs_release_read_binary(void);

// Extern C function: fs_read_binary_tuple
_tuple_string_int_string fs_read_binary_tuple(const char*);

// Extern C function: fs_copy_raw
_tuple_int_int_string fs_copy_raw(const char*, const char*);

// Extern C function: fs_move_raw
_tuple_int_int_string fs_move_raw(const char*, const char*);

// Extern C function: fs_realpath_raw
_tuple_string_int_string fs_realpath_raw(const char*);

// Extern C function: fs_chmod_raw
_tuple_int_int_string fs_chmod_raw(const char*, int);

// Extern C function: dir_list_count
int dir_list_count(void*);

// Extern C function: dir_list_get
const char* dir_list_get(void*, int);

// Extern C function: dir_list_free
void dir_list_free(void*);

// Extern C function: path_join
const char* path_join(const char*, const char*);

// Extern C function: path_dirname
const char* path_dirname(const char*);

// Extern C function: path_basename
const char* path_basename(const char*);

// Extern C function: path_extension
const char* path_extension(const char*);

// Extern C function: path_is_absolute
int path_is_absolute(const char*);

// Extern C function: fs_glob_raw
void* fs_glob_raw(const char*);

// Extern C function: fs_glob_multi_raw
void* fs_glob_multi_raw(void*);

// Extern C function: string_concat
const char* string_concat(const char*, const char*);

// Extern C function: string_length
int string_length(const char*);

// Extern C function: string_new_with_length
void* string_new_with_length(const char*, int);

// Extern C function: malloc (libc-provided, declaration skipped)
// Extern C function: math_abs_int
int math_abs_int(int);

// Extern C function: math_abs_float
double math_abs_float(double);

// Extern C function: math_min_int
int math_min_int(int, int);

// Extern C function: math_max_int
int math_max_int(int, int);

// Extern C function: math_min_float
double math_min_float(double, double);

// Extern C function: math_max_float
double math_max_float(double, double);

// Extern C function: math_clamp_int
int math_clamp_int(int, int, int);

// Extern C function: math_clamp_float
double math_clamp_float(double, double, double);

// Extern C function: math_sqrt
double math_sqrt(double);

// Extern C function: math_pow
double math_pow(double, double);

// Extern C function: math_sin
double math_sin(double);

// Extern C function: math_cos
double math_cos(double);

// Extern C function: math_tan
double math_tan(double);

// Extern C function: math_asin
double math_asin(double);

// Extern C function: math_acos
double math_acos(double);

// Extern C function: math_atan
double math_atan(double);

// Extern C function: math_atan2
double math_atan2(double, double);

// Extern C function: math_floor
double math_floor(double);

// Extern C function: math_ceil
double math_ceil(double);

// Extern C function: math_round
double math_round(double);

// Extern C function: math_log
double math_log(double);

// Extern C function: math_log10
double math_log10(double);

// Extern C function: math_exp
double math_exp(double);

// Extern C function: math_random_seed
void math_random_seed(int);

// Extern C function: math_random_int
int math_random_int(int, int);

// Extern C function: math_random_float
double math_random_float(void);

// Extern C function: math_pi
double math_pi(void);

// Extern C function: math_tau
double math_tau(void);

// Extern C function: math_e
double math_e(void);

// Extern C function: math_deg_to_rad
double math_deg_to_rad(void);

// Extern C function: math_rad_to_deg
double math_rad_to_deg(void);

// Extern C function: cryptography_sha1_hex_raw
const char* cryptography_sha1_hex_raw(const char*, int);

// Extern C function: cryptography_sha256_hex_raw
const char* cryptography_sha256_hex_raw(const char*, int);

// Extern C function: cryptography_hash_hex_raw
const char* cryptography_hash_hex_raw(const char*, const char*, int);

// Extern C function: cryptography_hash_supported
int cryptography_hash_supported(const char*);

// Extern C function: cryptography_base64_encode_raw
const char* cryptography_base64_encode_raw(const char*, int);

// Extern C function: cryptography_base64_encode_padded_raw
const char* cryptography_base64_encode_padded_raw(const char*, int);

// Extern C function: cryptography_base64_decode_raw
int cryptography_base64_decode_raw(const char*);

// Extern C function: cryptography_get_base64_decode
const char* cryptography_get_base64_decode(void);

// Extern C function: cryptography_get_base64_decode_length
int cryptography_get_base64_decode_length(void);

// Extern C function: cryptography_release_base64_decode
void cryptography_release_base64_decode(void);

// Extern C function: cryptography_md4_hex_raw
const char* cryptography_md4_hex_raw(const char*, int);

// Extern C function: cryptography_md5_hex_raw
const char* cryptography_md5_hex_raw(const char*, int);

// Extern C function: cryptography_hmac_sha256_hex_raw
const char* cryptography_hmac_sha256_hex_raw(const char*, int, const char*, int);

// Extern C function: cryptography_hmac_sha256_bytes_raw
int cryptography_hmac_sha256_bytes_raw(const char*, int, const char*, int);

// Extern C function: cryptography_md4_bytes_raw
int cryptography_md4_bytes_raw(const char*, int);

// Extern C function: cryptography_md5_bytes_raw
int cryptography_md5_bytes_raw(const char*, int);

// Extern C function: cryptography_sha1_bytes_raw
int cryptography_sha1_bytes_raw(const char*, int);

// Extern C function: cryptography_sha256_bytes_raw
int cryptography_sha256_bytes_raw(const char*, int);

// Extern C function: cryptography_hash_bytes_raw
int cryptography_hash_bytes_raw(const char*, const char*, int);

// Extern C function: cryptography_get_binary_digest
const char* cryptography_get_binary_digest(void);

// Extern C function: cryptography_get_binary_digest_length
int cryptography_get_binary_digest_length(void);

// Extern C function: cryptography_release_binary_digest
void cryptography_release_binary_digest(void);

// Extern C function: string_new_with_length
void* string_new_with_length(const char*, int);

// Extern C function: os_system
int os_system(const char*);

// Extern C function: os_exec_raw
const char* os_exec_raw(const char*);

// Extern C function: os_run
int os_run(const char*, void*, void*);

// Extern C function: os_run_capture_raw
const char* os_run_capture_raw(const char*, void*, void*);

// Extern C function: os_run_capture_status_raw
_tuple_string_int_string os_run_capture_status_raw(const char*, void*, void*);

// Extern C function: os_run_pipe_raw
_tuple_int_int_string os_run_pipe_raw(const char*, void*, void*);

// Extern C function: os_wait_pid_raw
_tuple_int_string os_wait_pid_raw(int);

// Extern C function: os_run_pipe_drain_and_wait_raw
_tuple_string_int_string os_run_pipe_drain_and_wait_raw(const char*, void*, void*);

// Extern C function: os_getenv
const char* os_getenv(const char*);

// Extern C function: io_setenv_raw
int io_setenv_raw(const char*, const char*);

// Extern C function: io_unsetenv_raw
int io_unsetenv_raw(const char*);

// Extern C function: os_which
const char* os_which(const char*);

// Extern C function: aether_args_count
int aether_args_count(void);

// Extern C function: aether_args_get
const char* aether_args_get(int);

// Extern C function: aether_argv0
const char* aether_argv0(void);

// Extern C function: aether_argv_raw
void* aether_argv_raw(void);

// Extern C function: aether_args_seal
void aether_args_seal(void);

// Extern C function: aether_args_sealed
int aether_args_sealed(void);

// Extern C function: os_execv
int os_execv(const char*, void*);

// Extern C function: string_concat
const char* string_concat(const char*, const char*);

// Extern C function: os_now_utc_iso8601_raw
const char* os_now_utc_iso8601_raw(void);

// Extern C function: os_now_local_fill_raw
void os_now_local_fill_raw(void*);

// Extern C function: os_now_local_iso8601_raw
const char* os_now_local_iso8601_raw(void);

// Extern C function: malloc (libc-provided, declaration skipped)
// Extern C function: os_platform_raw
const char* os_platform_raw(void);

// Extern C function: os_getpid_raw
int os_getpid_raw(void);

// Extern C function: exit (libc-provided, declaration skipped)
// Extern C function: os_wall_seconds_raw
int64_t os_wall_seconds_raw(void);

// Extern C function: os_wall_micros_raw
int os_wall_micros_raw(void);

// Extern C function: os_now_monotonic_ms_raw
int64_t os_now_monotonic_ms_raw(void);

// Extern C function: os_now_monotonic_ns_raw
int64_t os_now_monotonic_ns_raw(void);

// Extern C function: os_now_unix_ms_raw
int64_t os_now_unix_ms_raw(void);

// Extern C function: malloc (libc-provided, declaration skipped)
// Extern C function: free (libc-provided, declaration skipped)
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

// Extern C function: zsync_dup16
const char* zsync_dup16(void*);


// Import: std.string
// Import: std.fs
// Import: cmd.mklib
// Import: test.assert
#line 312 "/home/paul/scm/aether/build/../std/fs/module.ae"
static _tuple_int_string fs_mtime(const char* path) {
#line 313 "/home/paul/scm/aether/build/../std/fs/module.ae"
int m = file_mtime_raw(aether_string_data(path));
if (m < 0) {
        {
#line 315 "/home/paul/scm/aether/build/../std/fs/module.ae"
            return (_tuple_int_string){0, "cannot stat file"};
        }
    }
#line 317 "/home/paul/scm/aether/build/../std/fs/module.ae"
    return (_tuple_int_string){m, ""};
}

#line 515 "/home/paul/scm/aether/build/../std/fs/module.ae"
static _tuple_string_int_string fs_read_binary(const char* path) {
#line 521 "/home/paul/scm/aether/build/../std/fs/module.ae"
    return fs_read_binary_tuple(aether_string_data(path));
}

#line 19 "cmd/mklib.ae"
static int mklib_f2i(double x) {
#line 20 "cmd/mklib.ae"
    return x;
}

#line 22 "cmd/mklib.ae"
static double mklib_i2f(int x) {
#line 23 "cmd/mklib.ae"
    return (x * 1.0);
}

#line 26 "cmd/mklib.ae"
static const char* mklib_VERSION(void) {
#line 27 "cmd/mklib.ae"
    return "0.7.1";
}

#line 32 "cmd/mklib.ae"
static _tuple_int_int_int mklib_determine_hash_lengths(int file_len, int blocksize) {
#line 33 "cmd/mklib.ae"
double length = mklib_i2f(file_len);
#line 34 "cmd/mklib.ae"
double bs = mklib_i2f(blocksize);
#line 35 "cmd/mklib.ae"
int seq_matches = 1;
#line 37 "cmd/mklib.ae"
int rsum_len = mklib_f2i(math_ceil((((mklib_log2f(length) + mklib_log2f(bs)) - 8.6) / 8.0)));
if (rsum_len > 4) {
        {
#line 40 "cmd/mklib.ae"
seq_matches = 2;
#line 41 "cmd/mklib.ae"
rsum_len = 4;
        }
    }
if (rsum_len < 2) {
        {
#line 44 "cmd/mklib.ae"
rsum_len = 2;
        }
    }
#line 49 "cmd/mklib.ae"
double sm = mklib_i2f(seq_matches);
#line 50 "cmd/mklib.ae"
double a = math_ceil(((((20.0 + mklib_log2f(length)) + mklib_log2f((1.0 + (length / bs)))) / sm) / 8.0));
#line 51 "cmd/mklib.ae"
double b = math_ceil(((20.0 + mklib_log2f((1.0 + (length / bs)))) / 8.0));
#line 52 "cmd/mklib.ae"
int checksum_len = mklib_f2i(a);
if (mklib_f2i(b) > checksum_len) {
        {
#line 54 "cmd/mklib.ae"
checksum_len = mklib_f2i(b);
        }
    }
if (checksum_len > 16) {
        {
#line 57 "cmd/mklib.ae"
checksum_len = 16;
        }
    }
if (checksum_len < 4) {
        {
#line 60 "cmd/mklib.ae"
checksum_len = 4;
        }
    }
#line 62 "cmd/mklib.ae"
    return (_tuple_int_int_int){seq_matches, rsum_len, checksum_len};
}

#line 65 "cmd/mklib.ae"
static double mklib_log2f(double x) {
#line 66 "cmd/mklib.ae"
    return (math_log(x) / math_log(2.0));
}

#line 72 "cmd/mklib.ae"
static _tuple_string_int mklib_generate_n(const char* data, int data_len, int blocksize, const char* filename, const char* mtime, const char* url) {
    int _heap_out = 0; (void)_heap_out;
    const char* out = NULL;
    int _heap_head = 0; (void)_heap_head;
    const char* head = NULL;
#line 73 "cmd/mklib.ae"
{ const char* _tmp_old = out; out = mklib_generate(data, data_len, blocksize, filename, mtime, url); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 1; }
#line 74 "cmd/mklib.ae"
int nblocks = (((data_len + blocksize) - 1) / blocksize);
#line 75 "cmd/mklib.ae"
    _tuple_int_int_int _tup0 = mklib_determine_hash_lengths(data_len, blocksize);
    int sm = _tup0._0;
    int rsum_len = _tup0._1;
    int cksum_len = _tup0._2;
#line 76 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_build_header(data_len, blocksize, sm, rsum_len, cksum_len, filename, mtime, url); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 77 "cmd/mklib.ae"
int total = (string_length(head) + (nblocks * (rsum_len + cksum_len)));
#line 78 "cmd/mklib.ae"
    _tuple_string_int _builder_ret = (_tuple_string_int){aether_uniform_heap_str((const char*)(out), _heap_out), total};
    /* deferred */ if (_heap_head) { aether_heap_str_free(head); head = NULL; _heap_head = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_head) { aether_heap_str_free(head); head = NULL; _heap_head = 0; }
}

#line 82 "cmd/mklib.ae"
static const char* mklib_build_header(int data_len, int blocksize, int sm, int rsum_len, int cksum_len, const char* filename, const char* mtime, const char* url) {
    int _heap_sha1 = 0; (void)_heap_sha1;
    const char* sha1 = NULL;
    int _heap_e = 0; (void)_heap_e;
    const char* e = NULL;
    int _heap_head = 0; (void)_heap_head;
    const char* head = NULL;
#line 83 "cmd/mklib.ae"
    _tuple_string_string _tup1 = cryptography_sha1_hex("", 0);
    { const char* _tmp_old = sha1; sha1 = _tup1._0; if (_heap_sha1) aether_heap_str_free(_tmp_old); _heap_sha1 = 0; }
    { const char* _tmp_old = e; e = _tup1._1; if (_heap_e) aether_heap_str_free(_tmp_old); _heap_e = 0; }
#line 84 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ""; if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 0; }
#line 85 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "zsync: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 86 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, mklib_VERSION()); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 87 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
if (strcmp(_aether_safe_str(filename), _aether_safe_str("")) != 0) {
        {
#line 89 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "Filename: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 90 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, filename); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 91 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
if (strcmp(_aether_safe_str(mtime), _aether_safe_str("")) != 0) {
                {
#line 93 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "MTime: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 94 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, mtime); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 95 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
                }
            }
        }
    }
#line 98 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "Blocksize: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 99 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ({ const char* _ad_0 = (const char*)(mklib_int_str(blocksize)); const char* _ad_r = mklib_cat(head, _ad_0); aether_heap_str_free(_ad_0); _ad_r; }); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 100 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\nLength: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 101 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ({ const char* _ad_1 = (const char*)(mklib_int_str(data_len)); const char* _ad_r = mklib_cat(head, _ad_1); aether_heap_str_free(_ad_1); _ad_r; }); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 102 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\nHash-Lengths: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 103 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ({ const char* _ad_2 = (const char*)(mklib_int_str(sm)); const char* _ad_r = mklib_cat(head, _ad_2); aether_heap_str_free(_ad_2); _ad_r; }); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 104 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, ","); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 105 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ({ const char* _ad_3 = (const char*)(mklib_int_str(rsum_len)); const char* _ad_r = mklib_cat(head, _ad_3); aether_heap_str_free(_ad_3); _ad_r; }); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 106 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, ","); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 107 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ({ const char* _ad_4 = (const char*)(mklib_int_str(cksum_len)); const char* _ad_r = mklib_cat(head, _ad_4); aether_heap_str_free(_ad_4); _ad_r; }); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 108 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\nSHA-1: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 109 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "0000000000000000000000000000000000000000"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 110 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
if (strcmp(_aether_safe_str(url), _aether_safe_str("")) != 0) {
        {
#line 112 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "URL: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 113 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, url); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 114 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
        }
    }
#line 116 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 117 "cmd/mklib.ae"
    const char* _builder_ret = aether_uniform_heap_str((const char*)(head), _heap_head);
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_sha1) { aether_heap_str_free(sha1); sha1 = NULL; _heap_sha1 = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_sha1) { aether_heap_str_free(sha1); sha1 = NULL; _heap_sha1 = 0; }
}

#line 120 "cmd/mklib.ae"
static const char* mklib_generate(const char* data, int data_len, int blocksize, const char* filename, const char* mtime, const char* url) {
    int _heap_sha1 = 0; (void)_heap_sha1;
    const char* sha1 = NULL;
    int _heap_e = 0; (void)_heap_e;
    const char* e = NULL;
    int _heap_head = 0; (void)_heap_head;
    const char* head = NULL;
    int _heap_block = 0; (void)_heap_block;
    const char* block = NULL;
    int _heap_md = 0; (void)_heap_md;
    const char* md = NULL;
#line 121 "cmd/mklib.ae"
int nblocks = (((data_len + blocksize) - 1) / blocksize);
#line 123 "cmd/mklib.ae"
    _tuple_int_int_int _tup2 = mklib_determine_hash_lengths(data_len, blocksize);
    int sm = _tup2._0;
    int rsum_len = _tup2._1;
    int cksum_len = _tup2._2;
#line 126 "cmd/mklib.ae"
    _tuple_string_string _tup3 = cryptography_sha1_hex(data, data_len);
    { const char* _tmp_old = sha1; sha1 = _tup3._0; if (_heap_sha1) aether_heap_str_free(_tmp_old); _heap_sha1 = 0; }
    { const char* _tmp_old = e; e = _tup3._1; if (_heap_e) aether_heap_str_free(_tmp_old); _heap_e = 0; }
#line 129 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ""; if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 0; }
#line 130 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "zsync: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 131 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, mklib_VERSION()); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 132 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
if (strcmp(_aether_safe_str(filename), _aether_safe_str("")) != 0) {
        {
#line 134 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "Filename: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 135 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, filename); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 136 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
if (strcmp(_aether_safe_str(mtime), _aether_safe_str("")) != 0) {
                {
#line 138 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "MTime: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 139 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, mtime); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 140 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
                }
            }
        }
    }
#line 143 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "Blocksize: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 144 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ({ const char* _ad_5 = (const char*)(mklib_int_str(blocksize)); const char* _ad_r = mklib_cat(head, _ad_5); aether_heap_str_free(_ad_5); _ad_r; }); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 145 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\nLength: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 146 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ({ const char* _ad_6 = (const char*)(mklib_int_str(data_len)); const char* _ad_r = mklib_cat(head, _ad_6); aether_heap_str_free(_ad_6); _ad_r; }); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 147 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\nHash-Lengths: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 148 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ({ const char* _ad_7 = (const char*)(mklib_int_str(sm)); const char* _ad_r = mklib_cat(head, _ad_7); aether_heap_str_free(_ad_7); _ad_r; }); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 149 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, ","); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 150 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ({ const char* _ad_8 = (const char*)(mklib_int_str(rsum_len)); const char* _ad_r = mklib_cat(head, _ad_8); aether_heap_str_free(_ad_8); _ad_r; }); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 151 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, ","); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 152 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = ({ const char* _ad_9 = (const char*)(mklib_int_str(cksum_len)); const char* _ad_r = mklib_cat(head, _ad_9); aether_heap_str_free(_ad_9); _ad_r; }); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 153 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\nSHA-1: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 154 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, sha1); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 155 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
if (strcmp(_aether_safe_str(url), _aether_safe_str("")) != 0) {
        {
#line 157 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "URL: "); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 158 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, url); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 159 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
        }
    }
#line 161 "cmd/mklib.ae"
{ const char* _tmp_old = head; head = mklib_cat(head, "\n"); if (_heap_head) aether_heap_str_free(_tmp_old); _heap_head = 1; }
#line 167 "cmd/mklib.ae"
int head_len = string_length(head);
#line 168 "cmd/mklib.ae"
int table_len = (nblocks * (rsum_len + cksum_len));
#line 169 "cmd/mklib.ae"
int total = (head_len + table_len);
#line 170 "cmd/mklib.ae"
void* buf = fileio_buf_alloc(total);
#line 173 "cmd/mklib.ae"
int i = 0;
while (i < head_len) {
        {
#line 175 "cmd/mklib.ae"
fileio_buf_set(buf, i, (string_char_at_n(head, head_len, i) & 0xff));
#line 176 "cmd/mklib.ae"
i = (i + 1);
        }
    }
#line 179 "cmd/mklib.ae"
int bp = head_len;
#line 180 "cmd/mklib.ae"
int blk = 0;
    int off;
    RSum* r;
    int rv;
    int j;
    int sh;
    int k;
while (blk < nblocks) {
        {
#line 182 "cmd/mklib.ae"
off = (blk * blocksize);
#line 184 "cmd/mklib.ae"
{ const char* _tmp_old = block; block = mklib_block_at(data, data_len, off, blocksize); if (_heap_block) aether_heap_str_free(_tmp_old); _heap_block = 1; }
#line 185 "cmd/mklib.ae"
r = checksums_calc_rsum_block(block, blocksize, 0, blocksize);
#line 188 "cmd/mklib.ae"
rv = (((checksums_rsum_a(r) & 0xffff) << 16) | (checksums_rsum_b(r) & 0xffff));
#line 189 "cmd/mklib.ae"
j = 0;
while (j < rsum_len) {
                {
#line 193 "cmd/mklib.ae"
sh = (8 * ((rsum_len - 1) - j));
#line 194 "cmd/mklib.ae"
fileio_buf_set(buf, bp, ((rv >> sh) & 0xff));
#line 195 "cmd/mklib.ae"
bp = (bp + 1);
#line 196 "cmd/mklib.ae"
j = (j + 1);
                }
            }
#line 199 "cmd/mklib.ae"
{ const char* _tmp_old = md; md = checksums_calc_checksum(block, blocksize); if (_heap_md) aether_heap_str_free(_tmp_old); _heap_md = 0; }
#line 200 "cmd/mklib.ae"
k = 0;
while (k < cksum_len) {
                {
#line 202 "cmd/mklib.ae"
fileio_buf_set(buf, bp, (string_char_at_n(md, 16, k) & 0xff));
#line 203 "cmd/mklib.ae"
bp = (bp + 1);
#line 204 "cmd/mklib.ae"
k = (k + 1);
                }
            }
#line 206 "cmd/mklib.ae"
blk = (blk + 1);
        }
    }
#line 209 "cmd/mklib.ae"
    const char* _builder_ret = aether_uniform_heap_str((const char*)(fileio_buf_to_str(buf, total)), 1);
    /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
    /* deferred */ if (_heap_block) { aether_heap_str_free(block); block = NULL; _heap_block = 0; }
    /* deferred */ if (_heap_head) { aether_heap_str_free(head); head = NULL; _heap_head = 0; }
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_sha1) { aether_heap_str_free(sha1); sha1 = NULL; _heap_sha1 = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_md) { aether_heap_str_free(md); md = NULL; _heap_md = 0; }
    /* deferred */ if (_heap_block) { aether_heap_str_free(block); block = NULL; _heap_block = 0; }
    /* deferred */ if (_heap_head) { aether_heap_str_free(head); head = NULL; _heap_head = 0; }
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_sha1) { aether_heap_str_free(sha1); sha1 = NULL; _heap_sha1 = 0; }
}

#line 213 "cmd/mklib.ae"
static const char* mklib_block_at(const char* data, int data_len, int off, int blocksize) {
#line 214 "cmd/mklib.ae"
int avail = (data_len - off);
if (avail >= blocksize) {
        {
#line 216 "cmd/mklib.ae"
            return aether_uniform_heap_str((const char*)(fileio_slice(data, data_len, off, blocksize)), 1);
        }
    }
#line 218 "cmd/mklib.ae"
    return aether_uniform_heap_str((const char*)(fileio_pad_block(data, data_len, off, avail, blocksize)), 1);
}

#line 221 "cmd/mklib.ae"
static const char* mklib_cat(const char* a, const char* b) {
#line 222 "cmd/mklib.ae"
    return aether_uniform_heap_str((const char*)(string_concat(a, b)), 1);
}

#line 225 "cmd/mklib.ae"
static const char* mklib_int_str(int v) {
#line 226 "cmd/mklib.ae"
    return aether_uniform_heap_str((const char*)(string_from_int(v)), 1);
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

// Import: std.math
// Import: std.cryptography
#line 72 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
static _tuple_string_string cryptography_sha1_hex(const char* data, int length) {
    int _heap_out = 0; (void)_heap_out;
    const char* out = NULL;
#line 73 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
{ const char* _tmp_old = out; out = cryptography_sha1_hex_raw(aether_string_data(data), length); if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 0; }
if (out == 0) {
        {
#line 75 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
            return (_tuple_string_string){"", "openssl unavailable"};
        }
    }
#line 77 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
    return (_tuple_string_string){out, ""};
}

#line 236 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
static _tuple_string_int_string cryptography_md4_bytes(const char* data, int length) {
    int _heap_raw = 0; (void)_heap_raw;
    const char* raw = NULL;
    int _heap_owned = 0; (void)_heap_owned;
    const char* owned = NULL;
#line 237 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
int ok = cryptography_md4_bytes_raw(aether_string_data(data), length);
if (ok == 0) {
        {
#line 239 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
            _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(""), 0), 0, "md4 unavailable"};
            /* deferred */ if (_heap_raw) { aether_heap_str_free(raw); raw = NULL; _heap_raw = 0; }
            return _builder_ret;
        }
    }
#line 241 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
{ const char* _tmp_old = raw; raw = cryptography_get_binary_digest(); if (_heap_raw) aether_heap_str_free(_tmp_old); _heap_raw = 0; }
#line 242 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
int n = cryptography_get_binary_digest_length();
#line 243 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
{ const char* _tmp_old = owned; owned = string_new_with_length(raw, n); if (_heap_owned) aether_heap_str_free(_tmp_old); _heap_owned = 1; }
#line 244 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
cryptography_release_binary_digest();
#line 245 "/home/paul/scm/aether/build/../std/cryptography/module.ae"
    _tuple_string_int_string _builder_ret = (_tuple_string_int_string){aether_uniform_heap_str((const char*)(owned), _heap_owned), n, ""};
    /* deferred */ if (_heap_raw) { aether_heap_str_free(raw); raw = NULL; _heap_raw = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_raw) { aether_heap_str_free(raw); raw = NULL; _heap_raw = 0; }
}

// Import: std.os
// Import: rcksum.checksums
#line 32 "rcksum/checksums.ae"
static RSum* checksums_new_rsum(int a, int b) {
#line 33 "rcksum/checksums.ae"
void* raw = malloc(16);
#line 34 "rcksum/checksums.ae"
RSum* r = ((RSum*)(raw));
#line 35 "rcksum/checksums.ae"
(r->a = (a & 0xffff));
#line 36 "rcksum/checksums.ae"
(r->b = (b & 0xffff));
#line 37 "rcksum/checksums.ae"
    return r;
}

#line 46 "rcksum/checksums.ae"
static int checksums_rsum_a(RSum* r) {
#line 47 "rcksum/checksums.ae"
    return r->a;
}

#line 50 "rcksum/checksums.ae"
static int checksums_rsum_b(RSum* r) {
#line 51 "rcksum/checksums.ae"
    return r->b;
}

#line 58 "rcksum/checksums.ae"
static RSum* checksums_calc_rsum_block(const char* data, int data_len, int off, int len) {
#line 59 "rcksum/checksums.ae"
int a = 0;
#line 60 "rcksum/checksums.ae"
int b = 0;
#line 61 "rcksum/checksums.ae"
int i = 0;
    int c;
while (i < len) {
        {
#line 63 "rcksum/checksums.ae"
c = checksums_byte_at(data, data_len, (off + i));
#line 64 "rcksum/checksums.ae"
a = ((a + c) & 0xffff);
#line 65 "rcksum/checksums.ae"
b = ((b + a) & 0xffff);
#line 66 "rcksum/checksums.ae"
i = (i + 1);
        }
    }
#line 68 "rcksum/checksums.ae"
    return checksums_new_rsum(a, b);
}

#line 83 "rcksum/checksums.ae"
static const char* checksums_calc_checksum(const char* data, int len) {
    int _heap_digest = 0; (void)_heap_digest;
    const char* digest = NULL;
    int _heap_err = 0; (void)_heap_err;
    const char* err = NULL;
#line 84 "rcksum/checksums.ae"
    _tuple_string_int_string _tup4 = cryptography_md4_bytes(data, len);
    { const char* _tmp_old = digest; digest = _tup4._0; if (_heap_digest) aether_heap_str_free(_tmp_old); _heap_digest = 1; }
    int n = _tup4._1;
    { const char* _tmp_old = err; err = _tup4._2; if (_heap_err) aether_heap_str_free(_tmp_old); _heap_err = 0; }
if (strcmp(_aether_safe_str(err), _aether_safe_str("")) != 0) {
        {
#line 86 "rcksum/checksums.ae"
            const char* _builder_ret = "";
            if (_heap_digest) { aether_heap_str_free(digest); digest = NULL; _heap_digest = 0; }
            /* deferred */ if (_heap_err) { aether_heap_str_free(err); err = NULL; _heap_err = 0; }
            return _builder_ret;
        }
    }
#line 88 "rcksum/checksums.ae"
    const char* _builder_ret = digest;
    /* deferred */ if (_heap_err) { aether_heap_str_free(err); err = NULL; _heap_err = 0; }
    return _builder_ret;
    /* deferred */ if (_heap_err) { aether_heap_str_free(err); err = NULL; _heap_err = 0; }
}

#line 95 "rcksum/checksums.ae"
static int checksums_byte_at(const char* data, int data_len, int i) {
#line 96 "rcksum/checksums.ae"
    return (string_char_at_n(data, data_len, i) & 0xff);
}

// Import: rcksum.fileio
#line 79 "rcksum/fileio.ae"
static void* fileio_buf_alloc(int n) {
#line 80 "rcksum/fileio.ae"
    return zsync_buf_alloc(n);
}

#line 93 "rcksum/fileio.ae"
static void fileio_buf_set(void* b, int i, int v) {
#line 94 "rcksum/fileio.ae"
zsync_buf_set(b, i, v);
}

#line 108 "rcksum/fileio.ae"
static const char* fileio_buf_to_str(void* b, int n) {
    int _heap_s = 0; (void)_heap_s;
    const char* s = NULL;
#line 109 "rcksum/fileio.ae"
s = string_new_with_length(fileio_buf_as_string(b), n);
#line 113 "rcksum/fileio.ae"
string_retain(s);
#line 114 "rcksum/fileio.ae"
    const char* _no_defer_ret = aether_uniform_heap_str((const char*)(s), _heap_s);
    return _no_defer_ret;
}

#line 135 "rcksum/fileio.ae"
static const char* fileio_pad_block(const char* src, int src_len, int start, int rem, int blocksize) {
#line 136 "rcksum/fileio.ae"
void* out = zsync_buf_alloc(blocksize);
#line 137 "rcksum/fileio.ae"
int i = 0;
while (i < rem) {
        {
#line 139 "rcksum/fileio.ae"
zsync_buf_set(out, i, (string_char_at_n(src, src_len, (start + i)) & 0xff));
#line 140 "rcksum/fileio.ae"
i = (i + 1);
        }
    }
#line 142 "rcksum/fileio.ae"
    return aether_uniform_heap_str((const char*)(string_new_with_length(fileio_buf_as_string(out), blocksize)), 1);
}

#line 149 "rcksum/fileio.ae"
static const char* fileio_slice(const char* src, int src_len, int start, int len) {
#line 150 "rcksum/fileio.ae"
void* out = zsync_buf_alloc(len);
#line 151 "rcksum/fileio.ae"
int i = 0;
while (i < len) {
        {
#line 153 "rcksum/fileio.ae"
zsync_buf_set(out, i, (string_char_at_n(src, src_len, (start + i)) & 0xff));
#line 154 "rcksum/fileio.ae"
i = (i + 1);
        }
    }
#line 156 "rcksum/fileio.ae"
    return aether_uniform_heap_str((const char*)(string_new_with_length(fileio_buf_as_string(out), len)), 1);
}

#line 161 "rcksum/fileio.ae"
static const char* fileio_buf_as_string(void* b) {
#line 162 "rcksum/fileio.ae"
    return zsync_buf_identity(b);
}

int main(int argc, char** argv) {
    #ifdef _WIN32
    SetConsoleOutputCP(65001);  // CP_UTF8
    SetConsoleCP(65001);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    #endif
    aether_args_init(argc, argv);
    
    int _heap_data = 0; (void)_heap_data;
    const char* data = NULL;
    int _heap_e = 0; (void)_heap_e;
    const char* e = NULL;
    int _heap_mtime = 0; (void)_heap_mtime;
    const char* mtime = NULL;
    int _heap_me = 0; (void)_heap_me;
    const char* me = NULL;
    int _heap_ref = 0; (void)_heap_ref;
    const char* ref = NULL;
    int _heap_re = 0; (void)_heap_re;
    const char* re = NULL;
    int _heap_out = 0; (void)_heap_out;
    const char* out = NULL;
    {
#line 17 "cmd/zsyncmake_test.ae"
puts("=== zsyncmake byte-parity vs Go ===");
#line 18 "cmd/zsyncmake_test.ae"
Harness* h = assert_new();
#line 20 "cmd/zsyncmake_test.ae"
        _tuple_string_int_string _tup5 = fs_read_binary("/tmp/ctltest.dat");
        { const char* _tmp_old = data; data = _tup5._0; if (_heap_data) aether_heap_str_free(_tmp_old); _heap_data = 1; }
        int dlen = _tup5._1;
        { const char* _tmp_old = e; e = _tup5._2; if (_heap_e) aether_heap_str_free(_tmp_old); _heap_e = 0; }
#line 21 "cmd/zsyncmake_test.ae"
assert_eq_str(h, e, "", "read input");
#line 23 "cmd/zsyncmake_test.ae"
        _tuple_string_string_int _tup6 = read_line("/tmp/go_mtime.txt");
        { const char* _tmp_old = mtime; mtime = _tup6._0; if (_heap_mtime) aether_heap_str_free(_tmp_old); _heap_mtime = 1; }
        { const char* _tmp_old = me; me = _tup6._1; if (_heap_me) aether_heap_str_free(_tmp_old); _heap_me = 1; }
        int mk = _tup6._2;
#line 24 "cmd/zsyncmake_test.ae"
        _tuple_string_int_string _tup7 = fs_read_binary("/tmp/go_ref.zsync");
        { const char* _tmp_old = ref; ref = _tup7._0; if (_heap_ref) aether_heap_str_free(_tmp_old); _heap_ref = 1; }
        int rlen = _tup7._1;
        { const char* _tmp_old = re; re = _tup7._2; if (_heap_re) aether_heap_str_free(_tmp_old); _heap_re = 0; }
#line 25 "cmd/zsyncmake_test.ae"
assert_eq_str(h, re, "", "read go reference");
#line 27 "cmd/zsyncmake_test.ae"
        _tuple_string_int _tup8 = mklib_generate_n(data, dlen, 1024, "ctltest.dat", mtime, "http://example/ctltest.dat");
        { const char* _tmp_old = out; out = _tup8._0; if (_heap_out) aether_heap_str_free(_tmp_old); _heap_out = 1; }
        int olen = _tup8._1;
#line 29 "cmd/zsyncmake_test.ae"
assert_eq_int(h, olen, rlen, _aether_interp("output length matches Go (%d vs %d)", olen, rlen));
#line 33 "cmd/zsyncmake_test.ae"
int same = 1;
#line 34 "cmd/zsyncmake_test.ae"
int firstdiff = -1;
#line 35 "cmd/zsyncmake_test.ae"
int i = 0;
#line 36 "cmd/zsyncmake_test.ae"
int n = olen;
if (rlen < n) {
            {
#line 37 "cmd/zsyncmake_test.ae"
n = rlen;
            }
        }
while (i < n) {
            {
if ((string_char_at_n(out, olen, i) & 0xff) != (string_char_at_n(ref, rlen, i) & 0xff)) {
                    {
#line 40 "cmd/zsyncmake_test.ae"
same = 0;
if (firstdiff < 0) {
                            {
#line 41 "cmd/zsyncmake_test.ae"
firstdiff = i;
                            }
                        }
                    }
                }
#line 43 "cmd/zsyncmake_test.ae"
i = (i + 1);
            }
        }
if (same == 0) {
            {
#line 46 "cmd/zsyncmake_test.ae"
printf("  first byte diff at offset %d", firstdiff); putchar('\n');
            }
        }
#line 48 "cmd/zsyncmake_test.ae"
assert_eq_int(h, same, 1, "byte-identical to Go zsyncmake output");
#line 50 "cmd/zsyncmake_test.ae"
assert_report(h);
    }
    /* deferred */ if (_heap_out) { aether_heap_str_free(out); out = NULL; _heap_out = 0; }
    /* deferred */ if (_heap_re) { aether_heap_str_free(re); re = NULL; _heap_re = 0; }
    /* deferred */ if (_heap_ref) { aether_heap_str_free(ref); ref = NULL; _heap_ref = 0; }
    /* deferred */ if (_heap_me) { aether_heap_str_free(me); me = NULL; _heap_me = 0; }
    /* deferred */ if (_heap_mtime) { aether_heap_str_free(mtime); mtime = NULL; _heap_mtime = 0; }
    /* deferred */ if (_heap_e) { aether_heap_str_free(e); e = NULL; _heap_e = 0; }
    /* deferred */ if (_heap_data) { aether_heap_str_free(data); data = NULL; _heap_data = 0; }
    return 0;
}
#line 54 "cmd/zsyncmake_test.ae"
_tuple_string_string_int read_line(const char* path) {
    int _heap_data = 0; (void)_heap_data;
    const char* data = NULL;
    int _heap_e = 0; (void)_heap_e;
    const char* e = NULL;
#line 55 "cmd/zsyncmake_test.ae"
    _tuple_string_int_string _tup9 = fs_read_binary(path);
    { const char* _tmp_old = data; data = _tup9._0; if (_heap_data) aether_heap_str_free(_tmp_old); _heap_data = 1; }
    int n = _tup9._1;
    { const char* _tmp_old = e; e = _tup9._2; if (_heap_e) aether_heap_str_free(_tmp_old); _heap_e = 0; }
if (strcmp(_aether_safe_str(e), _aether_safe_str("")) != 0) {
        {
#line 57 "cmd/zsyncmake_test.ae"
            return (_tuple_string_string_int){aether_uniform_heap_str((const char*)(""), 0), aether_uniform_heap_str((const char*)(e), _heap_e), 0};
        }
    }
#line 60 "cmd/zsyncmake_test.ae"
int i = 0;
while (i < n) {
        {
if ((string_char_at_n(data, n, i) & 0xff) == 10) {
                {
#line 63 "cmd/zsyncmake_test.ae"
                    return (_tuple_string_string_int){aether_uniform_heap_str((const char*)(string_substring(data, 0, i)), 1), aether_uniform_heap_str((const char*)(""), 0), 1};
                }
            }
#line 65 "cmd/zsyncmake_test.ae"
i = (i + 1);
        }
    }
#line 67 "cmd/zsyncmake_test.ae"
    return (_tuple_string_string_int){aether_uniform_heap_str((const char*)(data), _heap_data), aether_uniform_heap_str((const char*)(""), 0), 1};
}

